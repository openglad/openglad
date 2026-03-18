#pragma once

#include <openglad/gameplay/net_transport.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/websocket.h>
#endif

namespace og::sim {

namespace detail {

#ifdef __EMSCRIPTEN__
using EmscriptenResult = EMSCRIPTEN_RESULT;
using EmscriptenBool = EM_BOOL;
using WebSocketHandle = EMSCRIPTEN_WEBSOCKET_T;
using WebSocketCreateAttributes = ::EmscriptenWebSocketCreateAttributes;
using WebSocketOpenEvent = ::EmscriptenWebSocketOpenEvent;
using WebSocketMessageEvent = ::EmscriptenWebSocketMessageEvent;
using WebSocketErrorEvent = ::EmscriptenWebSocketErrorEvent;
using WebSocketCloseEvent = ::EmscriptenWebSocketCloseEvent;
using WebSocketOpenCallback = em_websocket_open_callback_func;
using WebSocketMessageCallback = em_websocket_message_callback_func;
using WebSocketErrorCallback = em_websocket_error_callback_func;
using WebSocketCloseCallback = em_websocket_close_callback_func;

inline constexpr EmscriptenBool kFalse = EM_FALSE;
inline constexpr EmscriptenBool kTrue = EM_TRUE;
inline constexpr EmscriptenResult kResultSuccess = EMSCRIPTEN_RESULT_SUCCESS;
inline constexpr EmscriptenResult kResultDeferred = EMSCRIPTEN_RESULT_DEFERRED;
inline constexpr EmscriptenResult kResultNotSupported =
    EMSCRIPTEN_RESULT_NOT_SUPPORTED;
inline constexpr EmscriptenResult kResultFailedNotDeferred =
    EMSCRIPTEN_RESULT_FAILED_NOT_DEFERRED;
inline constexpr EmscriptenResult kResultInvalidTarget =
    EMSCRIPTEN_RESULT_INVALID_TARGET;
inline constexpr EmscriptenResult kResultUnknownTarget =
    EMSCRIPTEN_RESULT_UNKNOWN_TARGET;
inline constexpr EmscriptenResult kResultInvalidParam =
    EMSCRIPTEN_RESULT_INVALID_PARAM;
inline constexpr EmscriptenResult kResultFailed = EMSCRIPTEN_RESULT_FAILED;
inline constexpr EmscriptenResult kResultNoData = EMSCRIPTEN_RESULT_NO_DATA;
inline constexpr EmscriptenResult kResultTimedOut =
    EMSCRIPTEN_RESULT_TIMED_OUT;
#else
using EmscriptenResult = int;
using EmscriptenBool = int;
using WebSocketHandle = int;

struct WebSocketCreateAttributes {
    const char* url = nullptr;
    const char* protocols = nullptr;
    EmscriptenBool createOnMainThread = 0;
};

struct WebSocketOpenEvent {
    WebSocketHandle socket = 0;
};

struct WebSocketMessageEvent {
    WebSocketHandle socket = 0;
    std::uint8_t* data = nullptr;
    std::uint32_t numBytes = 0;
    EmscriptenBool isText = 0;
};

struct WebSocketErrorEvent {
    WebSocketHandle socket = 0;
};

struct WebSocketCloseEvent {
    WebSocketHandle socket = 0;
    EmscriptenBool wasClean = 0;
    unsigned short code = 0;
    char reason[512] = {};
};

using WebSocketOpenCallback =
    EmscriptenBool (*)(int, const WebSocketOpenEvent*, void*);
using WebSocketMessageCallback =
    EmscriptenBool (*)(int, const WebSocketMessageEvent*, void*);
using WebSocketErrorCallback =
    EmscriptenBool (*)(int, const WebSocketErrorEvent*, void*);
using WebSocketCloseCallback =
    EmscriptenBool (*)(int, const WebSocketCloseEvent*, void*);

inline constexpr EmscriptenBool kFalse = 0;
inline constexpr EmscriptenBool kTrue = 1;
inline constexpr EmscriptenResult kResultSuccess = 0;
inline constexpr EmscriptenResult kResultDeferred = 1;
inline constexpr EmscriptenResult kResultNotSupported = -1;
inline constexpr EmscriptenResult kResultFailedNotDeferred = -2;
inline constexpr EmscriptenResult kResultInvalidTarget = -3;
inline constexpr EmscriptenResult kResultUnknownTarget = -4;
inline constexpr EmscriptenResult kResultInvalidParam = -5;
inline constexpr EmscriptenResult kResultFailed = -6;
inline constexpr EmscriptenResult kResultNoData = -7;
inline constexpr EmscriptenResult kResultTimedOut = -8;
#endif

struct EmscriptenWebSocketApi {
    void (*init_create_attributes)(WebSocketCreateAttributes* attributes) =
        nullptr;
    EmscriptenBool (*is_supported)() = nullptr;
    WebSocketHandle (*create)(WebSocketCreateAttributes* attributes) = nullptr;
    EmscriptenResult (*set_onopen)(WebSocketHandle socket,
                                   void* user_data,
                                   WebSocketOpenCallback callback) = nullptr;
    EmscriptenResult (*set_onmessage)(
        WebSocketHandle socket,
        void* user_data,
        WebSocketMessageCallback callback) = nullptr;
    EmscriptenResult (*set_onerror)(WebSocketHandle socket,
                                    void* user_data,
                                    WebSocketErrorCallback callback) = nullptr;
    EmscriptenResult (*set_onclose)(WebSocketHandle socket,
                                    void* user_data,
                                    WebSocketCloseCallback callback) = nullptr;
    EmscriptenResult (*get_ready_state)(WebSocketHandle socket,
                                        unsigned short* ready_state) = nullptr;
    EmscriptenResult (*send_binary)(WebSocketHandle socket,
                                    void* binary_data,
                                    std::uint32_t data_length) = nullptr;
    EmscriptenResult (*close)(WebSocketHandle socket,
                              unsigned short code,
                              const char* reason) = nullptr;
    EmscriptenResult (*destroy)(WebSocketHandle socket) = nullptr;
};

const EmscriptenWebSocketApi& emscripten_websocket_api() noexcept;

#if defined(TESTING)
void set_emscripten_websocket_api_for_testing(
    const EmscriptenWebSocketApi* api) noexcept;
#endif

} // namespace detail

class EmscriptenWebSocketTransport final : public ITransport
{
public:
    struct Options {
        PeerId remote_peer_id = 1;
        std::string protocols;
    };

    // Browser WebSocket callbacks are event-loop driven and only enqueue
    // connection/message/disconnection events here. The single remote peer
    // state is updated later on the game thread from poll(). A send while
    // disconnected is treated as a no-op.
    explicit EmscriptenWebSocketTransport(std::string url);
    EmscriptenWebSocketTransport(std::string url, Options options);
    ~EmscriptenWebSocketTransport() override;

    EmscriptenWebSocketTransport(const EmscriptenWebSocketTransport&) = delete;
    EmscriptenWebSocketTransport& operator=(
        const EmscriptenWebSocketTransport&) = delete;
    EmscriptenWebSocketTransport(EmscriptenWebSocketTransport&&) = delete;
    EmscriptenWebSocketTransport& operator=(
        EmscriptenWebSocketTransport&&) = delete;

    void send(PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override;
    [[nodiscard]] std::vector<ReceivedMessage> poll() override;
    void accept_connections() override;
    void disconnect(PeerId peer_id) override;
    [[nodiscard]] std::vector<PeerId> connected_peers() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace og::sim
