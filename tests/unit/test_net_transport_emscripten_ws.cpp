#include <openglad/platform/net_transport_emscripten_ws.h>
#include <openglad/platform/net_transport_relay_ws.h>

#include "net_transport_emscripten_ws_detail.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using og::sim::EmscriptenWebSocketTransport;
using og::sim::PeerId;
using og::sim::RelayWebSocketTransport;
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
using og::sim::detail::kResultDeferred;
using og::sim::detail::kResultFailed;
using og::sim::detail::kResultFailedNotDeferred;
using og::sim::detail::kResultInvalidParam;
using og::sim::detail::kResultInvalidTarget;
using og::sim::detail::kResultNoData;
using og::sim::detail::kResultNotSupported;
using og::sim::detail::kResultSuccess;
using og::sim::detail::kResultTimedOut;
using og::sim::detail::kResultUnknownTarget;
using og::sim::detail::kTrue;
using og::sim::detail::set_emscripten_websocket_api_for_testing;

constexpr unsigned short kReadyStateOpen = 1;

TEST(EmscriptenWebSocketTransportDefaults,
     host_default_api_reports_unsupported_without_override)
{
    set_emscripten_websocket_api_for_testing(nullptr);

    const EmscriptenWebSocketApi& api =
        og::sim::detail::emscripten_websocket_api();
    WebSocketCreateAttributes attributes;
    api.init_create_attributes(&attributes);
    EXPECT_EQ(nullptr, attributes.url);
    EXPECT_EQ(nullptr, attributes.protocols);
    EXPECT_EQ(kFalse, api.is_supported());
    EXPECT_EQ(og::sim::detail::kResultNotSupported,
              api.create(&attributes));
    EXPECT_EQ(og::sim::detail::kResultNotSupported,
              api.set_onopen(1, nullptr, nullptr));
    EXPECT_EQ(og::sim::detail::kResultNotSupported,
              api.set_onmessage(1, nullptr, nullptr));
    EXPECT_EQ(og::sim::detail::kResultNotSupported,
              api.set_onerror(1, nullptr, nullptr));
    EXPECT_EQ(og::sim::detail::kResultNotSupported,
              api.set_onclose(1, nullptr, nullptr));
    unsigned short ready_state = 0;
    EXPECT_EQ(og::sim::detail::kResultNotSupported,
              api.get_ready_state(1, &ready_state));
    std::array<std::uint8_t, 1> payload = {0x42};
    EXPECT_EQ(og::sim::detail::kResultNotSupported,
              api.send_binary(1, payload.data(), payload.size()));
    EXPECT_EQ(og::sim::detail::kResultNotSupported,
              api.close(1, 1000, "unit"));
    EXPECT_EQ(og::sim::detail::kResultNotSupported,
              api.destroy(1));

    EmscriptenWebSocketTransport transport("ws://example.test/socket");
    EXPECT_THROW(transport.accept_connections(), std::runtime_error);
}

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
        std::vector<std::pair<unsigned short, std::string>> close_requests;
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

    const std::vector<std::pair<unsigned short, std::string>>& close_requests(
        WebSocketHandle socket) const
    {
        return sockets_.at(socket).close_requests;
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

        std::vector<std::uint8_t> owned_payload;
        if (!payload.empty())
            owned_payload.assign(payload.begin(), payload.end());
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

        if (data_length == 0) {
            it->second.sent_payloads.emplace_back();
            return kResultSuccess;
        }
        it->second.sent_payloads.emplace_back(bytes, bytes + data_length);
        return kResultSuccess;
    }

    static EmscriptenResult close(WebSocketHandle socket,
                                  unsigned short code,
                                  const char* reason)
    {
        FakeWebSocketBackend& backend = self();
        auto it = backend.sockets_.find(socket);
        if (it == backend.sockets_.end())
            return kResultInvalidTarget;

        ++it->second.close_calls;
        it->second.close_requests.emplace_back(
            code, reason == nullptr ? std::string() : std::string(reason));
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

std::span<const std::uint8_t> as_bytes(std::string_view text);

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

    template <typename Transport>
    WebSocketHandle accept_transport(Transport& transport)
    {
        transport.accept_connections();
        return backend_.accept_socket();
    }

    template <typename Transport>
    WebSocketHandle connect_transport(Transport& transport)
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

    static RelayWebSocketTransport::Options make_relay_options()
    {
        RelayWebSocketTransport::Options options;
        options.protocols = "og-relay";
        return options;
    }

    WebSocketHandle connect_relay_with_peer(
        RelayWebSocketTransport& transport,
        PeerId local_peer_id = 7u,
        PeerId remote_peer_id = 9u)
    {
        const WebSocketHandle socket = connect_transport(transport);
        const std::string joined = std::format(
            R"({{"type":"joined","peer_id":{},"host":{}}})",
            local_peer_id,
            local_peer_id);
        const std::string peer_list = std::format(
            R"({{"type":"peer_list","peers":[{},{}],"host":{}}})",
            local_peer_id,
            remote_peer_id,
            local_peer_id);
        EXPECT_TRUE(backend_.emit_message(socket, as_bytes(joined), true));
        EXPECT_TRUE(backend_.emit_message(socket, as_bytes(peer_list), true));
        EXPECT_TRUE(transport.poll().empty());
        EXPECT_EQ(std::optional<PeerId>(local_peer_id),
                  transport.local_peer_id());
        EXPECT_EQ(std::optional<PeerId>(local_peer_id),
                  transport.host_peer_id());
        EXPECT_EQ((std::vector<PeerId>{remote_peer_id}),
                  transport.connected_peers());
        return socket;
    }

    FakeWebSocketBackend backend_;
};

std::span<const std::uint8_t> as_bytes(std::string_view text)
{
    return {
        reinterpret_cast<const std::uint8_t*>(text.data()),
        text.size(),
    };
}

template <typename Action>
std::string runtime_error_message(Action&& action)
{
    try
    {
        std::forward<Action>(action)();
    }
    catch (const std::runtime_error& error)
    {
        return error.what();
    }
    catch (...)
    {
        ADD_FAILURE() << "expected std::runtime_error";
        return {};
    }

    ADD_FAILURE() << "expected std::runtime_error";
    return {};
}

TEST_F(EmscriptenWebSocketTransportTest,
       constructors_and_idle_sends_reject_invalid_arguments)
{
    EXPECT_THROW(
        {
            EmscriptenWebSocketTransport transport("");
        },
        std::invalid_argument);

    EmscriptenWebSocketTransport::Options invalid_peer_options = make_options();
    invalid_peer_options.remote_peer_id = 0u;
    EXPECT_THROW(
        {
            EmscriptenWebSocketTransport transport(
                "ws://example.test/socket", invalid_peer_options);
        },
        std::invalid_argument);
    EXPECT_THROW(
        {
            RelayWebSocketTransport transport(" \t\n ");
        },
        std::invalid_argument);

    const std::uint8_t payload = 0x5au;
    const std::size_t oversized_length =
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) +
        1u;
    ASSERT_GT(oversized_length,
              static_cast<std::size_t>(
                  std::numeric_limits<std::uint32_t>::max()));

    EmscriptenWebSocketTransport direct(
        "ws://example.test/socket", make_options());
    EXPECT_THROW(direct.send(42u, nullptr, 1u), std::runtime_error);
    EXPECT_THROW(direct.send(42u, &payload, oversized_length),
                 std::runtime_error);
    EXPECT_NO_THROW(direct.send(7u, &payload, 1u));
    direct.disconnect(7u);
    EXPECT_EQ(og::sim::TransportLinkState::Connecting, direct.link_state());
    EXPECT_TRUE(direct.connected_peers().empty());

    RelayWebSocketTransport relay(
        "ws://relay.example/api/room/GLAD-VALIDATION",
        make_relay_options());
    EXPECT_THROW(relay.send(9u, nullptr, 1u), std::runtime_error);
    EXPECT_THROW(relay.send(9u, &payload, oversized_length),
                 std::runtime_error);
    EXPECT_THROW(relay.broadcast(nullptr, 1u), std::runtime_error);
    EXPECT_THROW(relay.broadcast(&payload, oversized_length),
                 std::runtime_error);
    EXPECT_NO_THROW(relay.send(0u, &payload, 1u));
    EXPECT_NO_THROW(relay.broadcast(&payload, 1u));
    EXPECT_EQ(og::sim::TransportLinkState::Connecting, relay.link_state());
    EXPECT_TRUE(relay.connected_peers().empty());
}

TEST_F(EmscriptenWebSocketTransportTest,
       browser_result_diagnostics_preserve_specific_failure_names)
{
    struct ErrorCase {
        EmscriptenResult result;
        std::string_view name;
    };
    constexpr std::array<ErrorCase, 9> kErrorCases{{
        {kResultDeferred, "EMSCRIPTEN_RESULT_DEFERRED"},
        {kResultNotSupported, "EMSCRIPTEN_RESULT_NOT_SUPPORTED"},
        {kResultFailedNotDeferred,
         "EMSCRIPTEN_RESULT_FAILED_NOT_DEFERRED"},
        {kResultInvalidTarget, "EMSCRIPTEN_RESULT_INVALID_TARGET"},
        {kResultUnknownTarget, "EMSCRIPTEN_RESULT_UNKNOWN_TARGET"},
        {kResultInvalidParam, "EMSCRIPTEN_RESULT_INVALID_PARAM"},
        {kResultNoData, "EMSCRIPTEN_RESULT_NO_DATA"},
        {kResultTimedOut, "EMSCRIPTEN_RESULT_TIMED_OUT"},
        {12345, "EMSCRIPTEN_RESULT_UNKNOWN"},
    }};

    for (const ErrorCase& error_case : kErrorCases)
    {
        SCOPED_TRACE(error_case.name);
        backend_.set_onopen_result = error_case.result;
        EmscriptenWebSocketTransport transport(
            "ws://example.test/diagnostics", make_options());

        const std::string message = runtime_error_message(
            [&] { transport.accept_connections(); });
        EXPECT_NE(std::string::npos, message.find(error_case.name));
        EXPECT_NE(std::string::npos,
                  message.find(std::to_string(error_case.result)));

        const WebSocketHandle socket = backend_.last_created_socket;
        EXPECT_GE(backend_.close_calls(socket), 1);
        EXPECT_EQ(1, backend_.destroy_calls(socket));
        backend_.set_onopen_result = kResultSuccess;
    }
}

TEST_F(EmscriptenWebSocketTransportTest,
       creation_support_and_relay_callback_failures_are_reported_and_cleaned_up)
{
    backend_.fail_create = true;
    backend_.create_error = kResultInvalidTarget;
    EmscriptenWebSocketTransport direct(
        "ws://example.test/create-failure", make_options());
    const std::string direct_create_error = runtime_error_message(
        [&] { direct.accept_connections(); });
    EXPECT_NE(std::string::npos,
              direct_create_error.find("ws://example.test/create-failure"));
    EXPECT_NE(std::string::npos,
              direct_create_error.find("EMSCRIPTEN_RESULT_INVALID_TARGET"));
    EXPECT_EQ(1, backend_.create_calls);

    backend_.create_error = kResultUnknownTarget;
    RelayWebSocketTransport relay_create(
        "ws://relay.example/api/room/GLAD-CREATE-FAIL",
        make_relay_options());
    const std::string relay_create_error = runtime_error_message(
        [&] { relay_create.accept_connections(); });
    EXPECT_NE(std::string::npos,
              relay_create_error.find("GLAD-CREATE-FAIL"));
    EXPECT_NE(std::string::npos,
              relay_create_error.find(
                  std::to_string(kResultUnknownTarget)));
    EXPECT_EQ(2, backend_.create_calls);

    backend_.fail_create = false;
    backend_.supported = false;
    RelayWebSocketTransport unsupported(
        "ws://relay.example/api/room/GLAD-UNSUPPORTED",
        make_relay_options());
    const std::string unsupported_error = runtime_error_message(
        [&] { unsupported.accept_connections(); });
    EXPECT_NE(std::string::npos,
              unsupported_error.find("requires browser WebSocket support"));
    EXPECT_EQ(2, backend_.create_calls);

    backend_.supported = true;
    backend_.set_onerror_result = kResultNoData;
    RelayWebSocketTransport callback_failure(
        "ws://relay.example/api/room/GLAD-CALLBACK-FAIL",
        make_relay_options());
    const std::string callback_error = runtime_error_message(
        [&] { callback_failure.accept_connections(); });
    EXPECT_NE(std::string::npos,
              callback_error.find("setting onerror callback"));
    EXPECT_NE(std::string::npos,
              callback_error.find(std::to_string(kResultNoData)));
    const WebSocketHandle callback_socket = backend_.last_created_socket;
    EXPECT_GE(backend_.close_calls(callback_socket), 1);
    EXPECT_EQ(1, backend_.destroy_calls(callback_socket));
    backend_.set_onerror_result = kResultSuccess;
}

TEST_F(EmscriptenWebSocketTransportTest,
       accept_is_idempotent_and_heap_destruction_disposes_each_socket_once)
{
    auto direct = std::make_unique<EmscriptenWebSocketTransport>(
        "ws://example.test/idempotent", make_options());
    const WebSocketHandle direct_socket = accept_transport(*direct);
    direct->accept_connections();
    EXPECT_EQ(1, backend_.create_calls);
    ASSERT_TRUE(backend_.emit_open(direct_socket));
    EXPECT_TRUE(direct->poll().empty());
    EXPECT_EQ(og::sim::TransportLinkState::Connected, direct->link_state());

    direct.reset();
    EXPECT_GE(backend_.close_calls(direct_socket), 1);
    EXPECT_EQ(1, backend_.destroy_calls(direct_socket));

    auto relay = std::make_unique<RelayWebSocketTransport>(
        "ws://relay.example/api/room/GLAD-IDEMPOTENT",
        make_relay_options());
    const WebSocketHandle relay_socket = accept_transport(*relay);
    relay->accept_connections();
    EXPECT_EQ(2, backend_.create_calls);
    ASSERT_TRUE(backend_.emit_open(relay_socket));
    EXPECT_TRUE(relay->poll().empty());
    EXPECT_EQ(og::sim::TransportLinkState::Connected, relay->link_state());

    relay.reset();
    EXPECT_GE(backend_.close_calls(relay_socket), 1);
    EXPECT_EQ(1, backend_.destroy_calls(relay_socket));
}

TEST_F(EmscriptenWebSocketTransportTest,
       callbacks_from_retired_sockets_cannot_mutate_replacement_connections)
{
    const std::array<std::uint8_t, 1> payload{0x42u};

    EmscriptenWebSocketTransport direct(
        "ws://example.test/replacement", make_options());
    const WebSocketHandle old_direct_socket = connect_transport(direct);
    direct.disconnect(42u);
    ASSERT_EQ(1, backend_.destroy_calls(old_direct_socket));

    const WebSocketHandle new_direct_socket = accept_transport(direct);
    ASSERT_NE(old_direct_socket, new_direct_socket);
    ASSERT_TRUE(backend_.emit_open(old_direct_socket));
    ASSERT_TRUE(backend_.emit_message(old_direct_socket, payload));
    ASSERT_TRUE(backend_.emit_error(old_direct_socket));
    EXPECT_TRUE(direct.poll().empty());
    EXPECT_TRUE(direct.connected_peers().empty());
    EXPECT_EQ(og::sim::TransportLinkState::Connecting, direct.link_state());
    EXPECT_EQ(0, backend_.destroy_calls(new_direct_socket));

    ASSERT_TRUE(backend_.emit_open(new_direct_socket));
    EXPECT_TRUE(direct.poll().empty());
    EXPECT_EQ((std::vector<PeerId>{42u}), direct.connected_peers());

    RelayWebSocketTransport relay(
        "ws://relay.example/api/room/GLAD-REPLACEMENT",
        make_relay_options());
    const WebSocketHandle old_relay_socket = connect_transport(relay);
    ASSERT_TRUE(backend_.emit_close(old_relay_socket));
    EXPECT_TRUE(relay.poll().empty());
    ASSERT_EQ(1, backend_.destroy_calls(old_relay_socket));

    const WebSocketHandle new_relay_socket = accept_transport(relay);
    ASSERT_NE(old_relay_socket, new_relay_socket);
    ASSERT_TRUE(backend_.emit_open(old_relay_socket));
    ASSERT_TRUE(backend_.emit_message(old_relay_socket, payload));
    ASSERT_TRUE(backend_.emit_error(old_relay_socket));
    EXPECT_TRUE(relay.poll().empty());
    EXPECT_EQ(og::sim::TransportLinkState::Connecting, relay.link_state());
    EXPECT_FALSE(relay.local_peer_id().has_value());
    EXPECT_EQ(0, backend_.destroy_calls(new_relay_socket));

    ASSERT_TRUE(backend_.emit_open(new_relay_socket));
    EXPECT_TRUE(relay.poll().empty());
    EXPECT_EQ(og::sim::TransportLinkState::Connected, relay.link_state());
}

TEST_F(EmscriptenWebSocketTransportTest,
       receive_queues_are_bounded_and_close_before_accepting_more_messages)
{
    constexpr std::size_t kMaximumQueuedMessages = 1024u;
    const std::span<const std::uint8_t> empty_payload;

    EmscriptenWebSocketTransport direct(
        "ws://example.test/queue-limit", make_options());
    const WebSocketHandle direct_socket = connect_transport(direct);
    for (std::size_t index = 0; index <= kMaximumQueuedMessages; ++index)
    {
        ASSERT_TRUE(backend_.emit_message(direct_socket, empty_payload))
            << "message " << index;
    }

    ASSERT_FALSE(backend_.close_requests(direct_socket).empty());
    EXPECT_EQ(1008u, backend_.close_requests(direct_socket).front().first);
    EXPECT_EQ("receive queue full",
              backend_.close_requests(direct_socket).front().second);
    const std::vector<og::sim::ReceivedMessage> direct_messages = direct.poll();
    ASSERT_EQ(kMaximumQueuedMessages, direct_messages.size());
    EXPECT_TRUE(std::all_of(
        direct_messages.begin(),
        direct_messages.end(),
        [](const og::sim::ReceivedMessage& message) {
            return message.peer_id == 42u && message.data.empty();
        }));
    EXPECT_EQ(og::sim::TransportLinkState::Connected, direct.link_state());

    ASSERT_TRUE(backend_.emit_close(direct_socket));
    EXPECT_TRUE(direct.poll().empty());
    EXPECT_EQ(og::sim::TransportLinkState::Lost, direct.link_state());
    EXPECT_EQ(1, backend_.destroy_calls(direct_socket));

    RelayWebSocketTransport relay(
        "ws://relay.example/api/room/GLAD-QUEUE-LIMIT",
        make_relay_options());
    const WebSocketHandle relay_socket = connect_transport(relay);
    for (std::size_t index = 0; index <= kMaximumQueuedMessages; ++index)
    {
        ASSERT_TRUE(backend_.emit_message(relay_socket, empty_payload))
            << "message " << index;
    }

    ASSERT_FALSE(backend_.close_requests(relay_socket).empty());
    EXPECT_EQ(1008u, backend_.close_requests(relay_socket).front().first);
    EXPECT_EQ("receive queue full",
              backend_.close_requests(relay_socket).front().second);
    EXPECT_TRUE(relay.poll().empty());
    EXPECT_EQ(og::sim::TransportLinkState::Connected, relay.link_state());

    ASSERT_TRUE(backend_.emit_close(relay_socket));
    EXPECT_TRUE(relay.poll().empty());
    EXPECT_EQ(og::sim::TransportLinkState::Lost, relay.link_state());
    EXPECT_EQ(1, backend_.destroy_calls(relay_socket));
}

TEST_F(EmscriptenWebSocketTransportTest,
       relay_rejects_malformed_protocol_frames_without_corrupting_peer_state)
{
    RelayWebSocketTransport transport(
        "ws://relay.example/api/room/GLAD-PROTOCOL",
        make_relay_options());
    const WebSocketHandle socket = connect_transport(transport);
    transport.disconnect(20u);

    const std::array<std::string_view, 16> controls{{
        R"({"missing":"field"})",
        R"({"type"})",
        R"({"type":   )",
        R"({"type":7})",
        R"({"type":"unterminated})",
        R"({"type":"not\"recognized"})",
        R"({"type":"peer_joined","peer_id":17,"is_host":true})",
        R"({"type":"peer_left","peer_id":17})",
        R"({"type":"peer_joined","peer_id":4294967296,"is_host":false})",
        R"({"type":"peer_joined","peer_id":"oops","is_host":false})",
        R"({"type":"peer_joined","peer_id":18})",
        R"({"type":"peer_joined","peer_id":19,"is_host":null})",
        R"({"type":"peer_list","peers":null,"host":17})",
        R"({"type":"host_changed","new_host":0})",
        R"({"type":"joined"})",
        R"({"type":"joined","peer_id":1,"host":17})",
    }};
    for (const std::string_view control : controls)
        ASSERT_TRUE(backend_.emit_message(socket, as_bytes(control), true));
    ASSERT_TRUE(backend_.emit_message(
        socket,
        as_bytes(
            R"({"type":"peer_list","peers":[ 0 , 1 , 17 , 20 ],"host":17})"),
        true));

    ASSERT_TRUE(backend_.emit_invalid_binary_message(socket, 3u));
    const std::array<std::uint8_t, 3> too_short{{2u, 17u, 0u}};
    const std::array<std::uint8_t, 5> wrong_tag{{9u, 17u, 0u, 0u, 0u}};
    const std::array<std::uint8_t, 5> zero_peer{{2u, 0u, 0u, 0u, 0u}};
    const std::array<std::uint8_t, 6> blocked_peer{
        {2u, 20u, 0u, 0u, 0u, 0xeeu}};
    const std::array<std::uint8_t, 6> valid_peer{
        {2u, 17u, 0u, 0u, 0u, 0xaau}};
    ASSERT_TRUE(backend_.emit_message(socket, too_short));
    ASSERT_TRUE(backend_.emit_message(socket, wrong_tag));
    ASSERT_TRUE(backend_.emit_message(socket, zero_peer));
    ASSERT_TRUE(backend_.emit_message(socket, blocked_peer));
    ASSERT_TRUE(backend_.emit_message(socket, valid_peer));

    const std::vector<og::sim::ReceivedMessage> received = transport.poll();
    ASSERT_EQ(1u, received.size());
    EXPECT_EQ(17u, received.front().peer_id);
    EXPECT_EQ((std::vector<std::uint8_t>{0xaau}), received.front().data);
    EXPECT_EQ(std::optional<PeerId>(1u), transport.local_peer_id());
    EXPECT_EQ(std::optional<PeerId>(17u), transport.host_peer_id());
    EXPECT_EQ((std::vector<PeerId>{17u}), transport.connected_peers());
    EXPECT_EQ(og::sim::TransportLinkState::Connected, transport.link_state());
    EXPECT_TRUE(backend_.close_requests(socket).empty());
}

TEST_F(EmscriptenWebSocketTransportTest,
       relay_oversized_control_frame_requests_policy_close_and_disposal)
{
    RelayWebSocketTransport transport(
        "ws://relay.example/api/room/GLAD-LARGE-CONTROL",
        make_relay_options());
    const WebSocketHandle socket = connect_transport(transport);
    const std::string oversized_control(8u * 1024u + 1u, 'x');

    ASSERT_TRUE(
        backend_.emit_message(socket, as_bytes(oversized_control), true));
    ASSERT_FALSE(backend_.close_requests(socket).empty());
    EXPECT_EQ(1009u, backend_.close_requests(socket).front().first);
    EXPECT_EQ("control message too large",
              backend_.close_requests(socket).front().second);

    EXPECT_TRUE(transport.poll().empty());
    EXPECT_EQ(og::sim::TransportLinkState::Lost, transport.link_state());
    EXPECT_EQ(1, backend_.destroy_calls(socket));
    EXPECT_FALSE(transport.local_peer_id().has_value());
    EXPECT_FALSE(transport.host_peer_id().has_value());
}

TEST_F(EmscriptenWebSocketTransportTest,
       zero_length_sends_preserve_browser_and_relay_wire_framing)
{
    EmscriptenWebSocketTransport direct(
        "ws://example.test/zero-length", make_options());
    const WebSocketHandle direct_socket = connect_transport(direct);
    direct.send(42u, nullptr, 0u);
    ASSERT_EQ(1u, backend_.sent_payloads(direct_socket).size());
    EXPECT_TRUE(backend_.sent_payloads(direct_socket).front().empty());

    RelayWebSocketTransport relay_without_peers(
        "ws://relay.example/api/room/GLAD-NO-PEERS",
        make_relay_options());
    const WebSocketHandle no_peer_socket =
        connect_transport(relay_without_peers);
    const std::uint8_t byte = 0x5au;
    relay_without_peers.broadcast(&byte, 1u);
    EXPECT_TRUE(backend_.sent_payloads(no_peer_socket).empty());

    RelayWebSocketTransport relay(
        "ws://relay.example/api/room/GLAD-ZERO-LENGTH",
        make_relay_options());
    const WebSocketHandle relay_socket = connect_relay_with_peer(relay);
    relay.send(0u, &byte, 1u);
    relay.send(99u, &byte, 1u);
    EXPECT_TRUE(backend_.sent_payloads(relay_socket).empty());

    relay.send(9u, nullptr, 0u);
    relay.broadcast(nullptr, 0u);
    ASSERT_EQ(2u, backend_.sent_payloads(relay_socket).size());
    EXPECT_EQ((std::vector<std::uint8_t>{1u, 9u, 0u, 0u, 0u}),
              backend_.sent_payloads(relay_socket)[0]);
    EXPECT_EQ((std::vector<std::uint8_t>{3u}),
              backend_.sent_payloads(relay_socket)[1]);
}

TEST_F(EmscriptenWebSocketTransportTest,
       relay_targeted_and_broadcast_failures_close_and_dispose_connections)
{
    const std::array<std::uint8_t, 2> payload{0x55u, 0x66u};

    {
        RelayWebSocketTransport transport(
            "ws://relay.example/api/room/GLAD-TARGET-READY",
            make_relay_options());
        const WebSocketHandle socket = connect_relay_with_peer(transport);
        backend_.socket_state(socket).ready_state_result = kResultTimedOut;

        transport.send(9u, payload.data(), payload.size());
        EXPECT_TRUE(backend_.sent_payloads(socket).empty());
        EXPECT_GE(backend_.close_calls(socket), 1);
        EXPECT_TRUE(transport.poll().empty());
        EXPECT_EQ(og::sim::TransportLinkState::Lost, transport.link_state());
        EXPECT_EQ(1, backend_.destroy_calls(socket));
    }

    {
        RelayWebSocketTransport transport(
            "ws://relay.example/api/room/GLAD-TARGET-SEND",
            make_relay_options());
        const WebSocketHandle socket = connect_relay_with_peer(transport);
        backend_.socket_state(socket).send_result = kResultFailed;

        transport.send(9u, payload.data(), payload.size());
        EXPECT_TRUE(backend_.sent_payloads(socket).empty());
        EXPECT_GE(backend_.close_calls(socket), 1);
        EXPECT_TRUE(transport.poll().empty());
        EXPECT_EQ(og::sim::TransportLinkState::Lost, transport.link_state());
        EXPECT_EQ(1, backend_.destroy_calls(socket));
    }

    {
        RelayWebSocketTransport transport(
            "ws://relay.example/api/room/GLAD-BROADCAST-READY",
            make_relay_options());
        const WebSocketHandle socket = connect_relay_with_peer(transport);
        backend_.socket_state(socket).ready_state = 0u;

        transport.broadcast(payload.data(), payload.size());
        EXPECT_TRUE(backend_.sent_payloads(socket).empty());
        EXPECT_GE(backend_.close_calls(socket), 1);
        EXPECT_TRUE(transport.poll().empty());
        EXPECT_EQ(og::sim::TransportLinkState::Lost, transport.link_state());
        EXPECT_EQ(1, backend_.destroy_calls(socket));
    }
}

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

    ASSERT_TRUE(backend_.emit_invalid_binary_message(socket, 128u * 1024u + 1u));
    EXPECT_TRUE(transport.poll().empty());
    EXPECT_TRUE(transport.connected_peers().empty());
    EXPECT_GE(backend_.close_calls(socket), 1);
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

TEST_F(EmscriptenWebSocketTransportTest,
       relay_transport_processes_control_messages_and_binary_frames)
{
    RelayWebSocketTransport transport(
        "ws://relay.example/api/room/GLAD-TEST",
        make_relay_options());

    const WebSocketHandle socket = connect_transport(transport);
    EXPECT_EQ("ws://relay.example/api/room/GLAD-TEST", backend_.last_url);
    EXPECT_EQ("og-relay", backend_.last_protocols);

    const std::string joined =
        R"({"type":"joined","peer_id":7,"host":7})";
    const std::string peer_list =
        R"({"type":"peer_list","peers":[7,9],"host":7})";
    const std::string peer_joined =
        R"({"type":"peer_joined","peer_id":11,"is_host":false})";
    const std::string host_changed =
        R"({"type":"host_changed","new_host":11})";
    const std::string peer_left =
        R"({"type":"peer_left","peer_id":9})";
    const std::array<std::uint8_t, 7> inbound_binary{
        2u, 9u, 0u, 0u, 0u, 0x10u, 0x20u,
    };

    ASSERT_TRUE(backend_.emit_message(socket, as_bytes(joined), true));
    ASSERT_TRUE(backend_.emit_message(socket, as_bytes(peer_list), true));
    ASSERT_TRUE(backend_.emit_message(socket, inbound_binary));
    ASSERT_TRUE(backend_.emit_message(socket, as_bytes(peer_joined), true));

    const std::vector<og::sim::ReceivedMessage> received = transport.poll();
    ASSERT_EQ(1u, received.size());
    EXPECT_EQ(9u, received.front().peer_id);
    EXPECT_EQ((std::vector<std::uint8_t>{0x10u, 0x20u}), received.front().data);
    EXPECT_EQ(std::optional<PeerId>(7u), transport.local_peer_id());
    EXPECT_EQ(std::optional<PeerId>(7u), transport.host_peer_id());
    EXPECT_EQ((std::vector<PeerId>{9u, 11u}), transport.connected_peers());

    ASSERT_TRUE(backend_.emit_message(socket, as_bytes(host_changed), true));
    ASSERT_TRUE(backend_.emit_message(socket, as_bytes(peer_left), true));
    EXPECT_TRUE(transport.poll().empty());
    EXPECT_EQ(std::optional<PeerId>(11u), transport.host_peer_id());
    EXPECT_EQ((std::vector<PeerId>{11u}), transport.connected_peers());

    transport.disconnect(11u);
    EXPECT_TRUE(transport.connected_peers().empty());
    EXPECT_FALSE(transport.host_peer_id().has_value());
    transport.disconnect(0u);
}

TEST_F(EmscriptenWebSocketTransportTest,
       relay_default_options_construct_and_oversized_frame_closes_socket)
{
    RelayWebSocketTransport transport(
        "ws://relay.example/api/room/GLAD-LARGE");
    const WebSocketHandle socket = connect_transport(transport);

    ASSERT_TRUE(backend_.emit_invalid_binary_message(
        socket, 128u * 1024u + 1u));
    EXPECT_TRUE(transport.poll().empty());
    EXPECT_TRUE(transport.connected_peers().empty());
    EXPECT_GE(backend_.close_calls(socket), 1);
    EXPECT_EQ(1, backend_.destroy_calls(socket));
}

TEST_F(EmscriptenWebSocketTransportTest,
       relay_transport_wraps_targeted_and_broadcast_payloads_for_browser_socket)
{
    RelayWebSocketTransport transport(
        "ws://relay.example/api/room/GLAD-TEST",
        make_relay_options());

    const WebSocketHandle socket = connect_transport(transport);
    const std::string joined =
        R"({"type":"joined","peer_id":7,"host":7})";
    const std::string peer_list =
        R"({"type":"peer_list","peers":[7,9,11],"host":7})";
    ASSERT_TRUE(backend_.emit_message(socket, as_bytes(joined), true));
    ASSERT_TRUE(backend_.emit_message(socket, as_bytes(peer_list), true));
    EXPECT_TRUE(transport.poll().empty());

    const std::array<std::uint8_t, 3> targeted_payload{0x01u, 0x02u, 0x03u};
    transport.send(9u, targeted_payload.data(), targeted_payload.size());

    const std::array<std::uint8_t, 2> broadcast_payload{0x04u, 0x05u};
    transport.broadcast(broadcast_payload.data(), broadcast_payload.size());

    ASSERT_EQ(2u, backend_.sent_payloads(socket).size());
    EXPECT_EQ((std::vector<std::uint8_t>{1u, 9u, 0u, 0u, 0u, 0x01u, 0x02u, 0x03u}),
              backend_.sent_payloads(socket)[0]);
    EXPECT_EQ((std::vector<std::uint8_t>{3u, 0x04u, 0x05u}),
              backend_.sent_payloads(socket)[1]);
}

TEST_F(EmscriptenWebSocketTransportTest,
       relay_broadcast_send_failure_disconnects_browser_transport_after_poll)
{
    RelayWebSocketTransport transport(
        "ws://relay.example/api/room/GLAD-TEST",
        make_relay_options());

    const WebSocketHandle socket = connect_transport(transport);
    const std::string joined =
        R"({"type":"joined","peer_id":7,"host":7})";
    const std::string peer_list =
        R"({"type":"peer_list","peers":[7,9],"host":7})";
    ASSERT_TRUE(backend_.emit_message(socket, as_bytes(joined), true));
    ASSERT_TRUE(backend_.emit_message(socket, as_bytes(peer_list), true));
    EXPECT_TRUE(transport.poll().empty());

    backend_.socket_state(socket).send_result = kResultFailed;
    const std::array<std::uint8_t, 2> payload{0x55u, 0x66u};
    transport.broadcast(payload.data(), payload.size());

    EXPECT_TRUE(transport.poll().empty());
    EXPECT_TRUE(transport.connected_peers().empty());
    EXPECT_FALSE(transport.local_peer_id().has_value());
    EXPECT_FALSE(transport.host_peer_id().has_value());
    EXPECT_GE(backend_.close_calls(socket), 1);
    EXPECT_EQ(1, backend_.destroy_calls(socket));
}

TEST_F(EmscriptenWebSocketTransportTest,
       link_state_tracks_connect_and_close_transitions_on_poll)
{
    EmscriptenWebSocketTransport transport("ws://example.test/socket",
                                           make_options());
    EXPECT_EQ(og::sim::TransportLinkState::Connecting, transport.link_state());

    const WebSocketHandle socket = accept_transport(transport);
    ASSERT_TRUE(backend_.emit_open(socket));
    // Browser callbacks only enqueue; the state flips on the poll() thread.
    EXPECT_EQ(og::sim::TransportLinkState::Connecting, transport.link_state());
    EXPECT_TRUE(transport.poll().empty());
    EXPECT_EQ(og::sim::TransportLinkState::Connected, transport.link_state());

    ASSERT_TRUE(backend_.emit_close(socket));
    EXPECT_TRUE(transport.poll().empty());
    EXPECT_EQ(og::sim::TransportLinkState::Lost, transport.link_state());
}

TEST_F(EmscriptenWebSocketTransportTest,
       link_state_reports_failed_when_error_arrives_before_open)
{
    EmscriptenWebSocketTransport transport("ws://example.test/socket",
                                           make_options());
    const WebSocketHandle socket = accept_transport(transport);

    ASSERT_TRUE(backend_.emit_error(socket));
    EXPECT_TRUE(transport.poll().empty());
    EXPECT_EQ(og::sim::TransportLinkState::Failed, transport.link_state());
}

TEST_F(EmscriptenWebSocketTransportTest,
       relay_link_state_reports_failed_before_open_and_lost_after_connect)
{
    // Failed: relay closes the socket before it ever opens (wrong or expired
    // room code, TLS/mixed-content rejection).
    {
        RelayWebSocketTransport transport(
            "ws://relay.example/api/room/GLAD-DEAD",
            make_relay_options());
        EXPECT_EQ(og::sim::TransportLinkState::Connecting,
                  transport.link_state());

        const WebSocketHandle socket = accept_transport(transport);
        ASSERT_TRUE(backend_.emit_close(socket));
        EXPECT_TRUE(transport.poll().empty());
        EXPECT_EQ(og::sim::TransportLinkState::Failed, transport.link_state());
    }

    // Lost: the relay socket drops after the room connection was up.
    RelayWebSocketTransport transport(
        "ws://relay.example/api/room/GLAD-TEST",
        make_relay_options());
    const WebSocketHandle socket = connect_transport(transport);
    EXPECT_EQ(og::sim::TransportLinkState::Connected, transport.link_state());

    ASSERT_TRUE(backend_.emit_error(socket));
    EXPECT_TRUE(transport.poll().empty());
    EXPECT_EQ(og::sim::TransportLinkState::Lost, transport.link_state());
}

} // namespace
