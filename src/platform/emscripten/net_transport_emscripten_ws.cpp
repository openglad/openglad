#include <openglad/platform/net_transport_emscripten_ws.h>

#include "net_transport_emscripten_ws_detail.h"

#include <deque>
#include <format>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace og::sim {
namespace {

constexpr unsigned short kWebSocketReadyStateOpen = 1;

#ifdef __EMSCRIPTEN__
void default_init_create_attributes(
    detail::WebSocketCreateAttributes* attributes)
{
    emscripten_websocket_init_create_attributes(attributes);
}

detail::EmscriptenBool default_is_supported()
{
    return emscripten_websocket_is_supported();
}

detail::WebSocketHandle default_create(
    detail::WebSocketCreateAttributes* attributes)
{
    return emscripten_websocket_new(attributes);
}

detail::EmscriptenResult default_set_onopen(
    detail::WebSocketHandle socket,
    void* user_data,
    detail::WebSocketOpenCallback callback)
{
    return emscripten_websocket_set_onopen_callback(
        socket, user_data, callback);
}

detail::EmscriptenResult default_set_onmessage(
    detail::WebSocketHandle socket,
    void* user_data,
    detail::WebSocketMessageCallback callback)
{
    return emscripten_websocket_set_onmessage_callback(
        socket, user_data, callback);
}

detail::EmscriptenResult default_set_onerror(
    detail::WebSocketHandle socket,
    void* user_data,
    detail::WebSocketErrorCallback callback)
{
    return emscripten_websocket_set_onerror_callback(
        socket, user_data, callback);
}

detail::EmscriptenResult default_set_onclose(
    detail::WebSocketHandle socket,
    void* user_data,
    detail::WebSocketCloseCallback callback)
{
    return emscripten_websocket_set_onclose_callback(
        socket, user_data, callback);
}

detail::EmscriptenResult default_get_ready_state(
    detail::WebSocketHandle socket,
    unsigned short* ready_state)
{
    return emscripten_websocket_get_ready_state(socket, ready_state);
}

detail::EmscriptenResult default_send_binary(
    detail::WebSocketHandle socket,
    void* binary_data,
    std::uint32_t data_length)
{
    return emscripten_websocket_send_binary(socket, binary_data, data_length);
}

detail::EmscriptenResult default_close(detail::WebSocketHandle socket,
                                       unsigned short code,
                                       const char* reason)
{
    return emscripten_websocket_close(socket, code, reason);
}

detail::EmscriptenResult default_destroy(detail::WebSocketHandle socket)
{
    return emscripten_websocket_delete(socket);
}
#else
void default_init_create_attributes(
    detail::WebSocketCreateAttributes* attributes)
{
    if (attributes != nullptr)
        *attributes = detail::WebSocketCreateAttributes{};
}

detail::EmscriptenBool default_is_supported()
{
    return detail::kFalse;
}

detail::WebSocketHandle default_create(
    detail::WebSocketCreateAttributes*)
{
    return detail::kResultNotSupported;
}

detail::EmscriptenResult default_set_onopen(detail::WebSocketHandle,
                                            void*,
                                            detail::WebSocketOpenCallback)
{
    return detail::kResultNotSupported;
}

detail::EmscriptenResult default_set_onmessage(
    detail::WebSocketHandle,
    void*,
    detail::WebSocketMessageCallback)
{
    return detail::kResultNotSupported;
}

detail::EmscriptenResult default_set_onerror(detail::WebSocketHandle,
                                             void*,
                                             detail::WebSocketErrorCallback)
{
    return detail::kResultNotSupported;
}

detail::EmscriptenResult default_set_onclose(detail::WebSocketHandle,
                                             void*,
                                             detail::WebSocketCloseCallback)
{
    return detail::kResultNotSupported;
}

detail::EmscriptenResult default_get_ready_state(
    detail::WebSocketHandle,
    unsigned short*)
{
    return detail::kResultNotSupported;
}

detail::EmscriptenResult default_send_binary(detail::WebSocketHandle,
                                             void*,
                                             std::uint32_t)
{
    return detail::kResultNotSupported;
}

detail::EmscriptenResult default_close(detail::WebSocketHandle,
                                       unsigned short,
                                       const char*)
{
    return detail::kResultNotSupported;
}

detail::EmscriptenResult default_destroy(detail::WebSocketHandle)
{
    return detail::kResultNotSupported;
}
#endif

const detail::EmscriptenWebSocketApi kDefaultWebSocketApi = {
    .init_create_attributes = &default_init_create_attributes,
    .is_supported = &default_is_supported,
    .create = &default_create,
    .set_onopen = &default_set_onopen,
    .set_onmessage = &default_set_onmessage,
    .set_onerror = &default_set_onerror,
    .set_onclose = &default_set_onclose,
    .get_ready_state = &default_get_ready_state,
    .send_binary = &default_send_binary,
    .close = &default_close,
    .destroy = &default_destroy,
};

#if defined(TESTING)
const detail::EmscriptenWebSocketApi* g_websocket_api_override = nullptr;
#endif

const char* emscripten_result_name(detail::EmscriptenResult result) noexcept
{
    switch (result)
    {
    case detail::kResultSuccess:
        return "EMSCRIPTEN_RESULT_SUCCESS";
    case detail::kResultDeferred:
        return "EMSCRIPTEN_RESULT_DEFERRED";
    case detail::kResultNotSupported:
        return "EMSCRIPTEN_RESULT_NOT_SUPPORTED";
    case detail::kResultFailedNotDeferred:
        return "EMSCRIPTEN_RESULT_FAILED_NOT_DEFERRED";
    case detail::kResultInvalidTarget:
        return "EMSCRIPTEN_RESULT_INVALID_TARGET";
    case detail::kResultUnknownTarget:
        return "EMSCRIPTEN_RESULT_UNKNOWN_TARGET";
    case detail::kResultInvalidParam:
        return "EMSCRIPTEN_RESULT_INVALID_PARAM";
    case detail::kResultFailed:
        return "EMSCRIPTEN_RESULT_FAILED";
    case detail::kResultNoData:
        return "EMSCRIPTEN_RESULT_NO_DATA";
    case detail::kResultTimedOut:
        return "EMSCRIPTEN_RESULT_TIMED_OUT";
    default:
        return "EMSCRIPTEN_RESULT_UNKNOWN";
    }
}

[[noreturn]] void throw_emscripten_error(std::string_view operation,
                                         detail::EmscriptenResult result)
{
    throw std::runtime_error(std::format(
        "EmscriptenWebSocketTransport {} failed: {} ({})",
        operation,
        emscripten_result_name(result),
        result));
}

void throw_if_emscripten_failed(std::string_view operation,
                                detail::EmscriptenResult result)
{
    if (result != detail::kResultSuccess)
        throw_emscripten_error(operation, result);
}

} // namespace

namespace detail {

const EmscriptenWebSocketApi& emscripten_websocket_api() noexcept
{
#if defined(TESTING)
    if (g_websocket_api_override != nullptr)
        return *g_websocket_api_override;
#endif
    return kDefaultWebSocketApi;
}

#if defined(TESTING)
void set_emscripten_websocket_api_for_testing(
    const EmscriptenWebSocketApi* api) noexcept
{
    g_websocket_api_override = api;
}
#endif

} // namespace detail

struct EmscriptenWebSocketTransport::Impl
{
    using WebSocketHandle = detail::WebSocketHandle;

    enum class QueueEntryKind : std::uint8_t {
        Connect,
        Message,
        Disconnect,
    };

    struct QueueEntry {
        QueueEntryKind kind = QueueEntryKind::Message;
        WebSocketHandle socket = 0;
        std::vector<std::uint8_t> payload;
    };

    Impl(std::string url_in, Options options_in)
        : url(std::move(url_in))
        , options(std::move(options_in))
    {
        if (url.empty())
        {
            throw std::invalid_argument(
                "EmscriptenWebSocketTransport URL must not be empty");
        }
        if (options.remote_peer_id == 0)
        {
            throw std::invalid_argument(
                "EmscriptenWebSocketTransport remote peer id must be non-zero");
        }
    }

    ~Impl()
    {
        connected = false;
        started = false;
        clear_queue();
        dispose_socket();
    }

    void accept_connections()
    {
        if (started)
            return;

        const detail::EmscriptenWebSocketApi& api =
            detail::emscripten_websocket_api();
        if (api.is_supported == nullptr || api.is_supported() == detail::kFalse)
        {
            throw std::runtime_error(
                "EmscriptenWebSocketTransport requires browser WebSocket support");
        }

        clear_queue();
        connected = false;

        detail::WebSocketCreateAttributes create_attributes{};
        api.init_create_attributes(&create_attributes);
        create_attributes.url = url.c_str();
        create_attributes.protocols =
            options.protocols.empty() ? nullptr : options.protocols.c_str();

        const WebSocketHandle new_socket = api.create(&create_attributes);
        if (new_socket <= 0)
        {
            throw std::runtime_error(std::format(
                "EmscriptenWebSocketTransport failed to open {}: {} ({})",
                url,
                emscripten_result_name(new_socket),
                new_socket));
        }

        socket = new_socket;
        try
        {
            throw_if_emscripten_failed(
                "setting onopen callback",
                api.set_onopen(socket, this, &Impl::on_open));
            throw_if_emscripten_failed(
                "setting onmessage callback",
                api.set_onmessage(socket, this, &Impl::on_message));
            throw_if_emscripten_failed(
                "setting onerror callback",
                api.set_onerror(socket, this, &Impl::on_error));
            throw_if_emscripten_failed(
                "setting onclose callback",
                api.set_onclose(socket, this, &Impl::on_close));
        }
        catch (...)
        {
            dispose_socket();
            clear_queue();
            throw;
        }

        started = true;
    }

    void send(PeerId peer_id, const std::uint8_t* data, std::size_t len)
    {
        if (data == nullptr && len != 0)
        {
            throw std::runtime_error(std::format(
                "EmscriptenWebSocketTransport peer {} send buffer is null",
                peer_id));
        }
        if (len > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error(std::format(
                "EmscriptenWebSocketTransport peer {} payload is too large: {}",
                peer_id,
                len));
        }
        if (peer_id != options.remote_peer_id || !connected || socket <= 0)
            return;

        const detail::EmscriptenWebSocketApi& api =
            detail::emscripten_websocket_api();
        unsigned short ready_state = 0;
        const detail::EmscriptenResult ready_state_result =
            api.get_ready_state(socket, &ready_state);
        if (ready_state_result != detail::kResultSuccess ||
            ready_state != kWebSocketReadyStateOpen)
        {
            enqueue_disconnect(socket);
            request_close_socket(socket);
            return;
        }

        std::uint8_t empty_payload = 0;
        void* payload = len == 0 ? static_cast<void*>(&empty_payload)
                                 : const_cast<std::uint8_t*>(data);
        const detail::EmscriptenResult send_result =
            api.send_binary(socket, payload, static_cast<std::uint32_t>(len));
        if (send_result != detail::kResultSuccess)
        {
            enqueue_disconnect(socket);
            request_close_socket(socket);
        }
    }

    std::vector<ReceivedMessage> poll()
    {
        std::deque<QueueEntry> queued_entries;
        {
            std::unique_lock<std::mutex> lock(queue_mutex, std::try_to_lock);
            if (!lock.owns_lock())
                return {};

            if (queue.empty())
                return {};

            queued_entries.swap(queue);
        }

        std::vector<ReceivedMessage> drained_messages;
        drained_messages.reserve(queued_entries.size());

        bool should_dispose_socket = false;
        for (auto& entry : queued_entries)
        {
            if (entry.socket != socket)
                continue;

            switch (entry.kind)
            {
            case QueueEntryKind::Connect:
                connected = true;
                break;

            case QueueEntryKind::Message:
                if (connected)
                {
                    drained_messages.push_back(
                        {options.remote_peer_id, std::move(entry.payload)});
                }
                break;

            case QueueEntryKind::Disconnect:
                connected = false;
                started = false;
                should_dispose_socket = true;
                break;
            }
        }

        if (should_dispose_socket)
            dispose_socket();

        return drained_messages;
    }

    void disconnect(PeerId peer_id)
    {
        if (peer_id != options.remote_peer_id)
            return;

        connected = false;
        started = false;
        clear_queue();
        dispose_socket();
        clear_queue();
    }

    std::vector<PeerId> connected_peers() const
    {
        if (!connected)
            return {};
        return {options.remote_peer_id};
    }

private:
    static detail::EmscriptenBool on_open(
        int,
        const detail::WebSocketOpenEvent* websocket_event,
        void* user_data)
    {
        if (websocket_event == nullptr || user_data == nullptr)
            return detail::kFalse;

        static_cast<Impl*>(user_data)->handle_connect(websocket_event->socket);
        return detail::kTrue;
    }

    static detail::EmscriptenBool on_message(
        int,
        const detail::WebSocketMessageEvent* websocket_event,
        void* user_data)
    {
        if (websocket_event == nullptr || user_data == nullptr)
            return detail::kFalse;

        static_cast<Impl*>(user_data)->handle_message(websocket_event);
        return detail::kTrue;
    }

    static detail::EmscriptenBool on_error(
        int,
        const detail::WebSocketErrorEvent* websocket_event,
        void* user_data)
    {
        if (websocket_event == nullptr || user_data == nullptr)
            return detail::kFalse;

        static_cast<Impl*>(user_data)->handle_disconnect(websocket_event->socket);
        return detail::kTrue;
    }

    static detail::EmscriptenBool on_close(
        int,
        const detail::WebSocketCloseEvent* websocket_event,
        void* user_data)
    {
        if (websocket_event == nullptr || user_data == nullptr)
            return detail::kFalse;

        static_cast<Impl*>(user_data)->handle_disconnect(websocket_event->socket);
        return detail::kTrue;
    }

    void handle_connect(WebSocketHandle socket_handle)
    {
        if (socket_handle != socket)
            return;

        QueueEntry entry;
        entry.kind = QueueEntryKind::Connect;
        entry.socket = socket_handle;
        enqueue(std::move(entry));
    }

    void handle_message(const detail::WebSocketMessageEvent* websocket_event)
    {
        if (websocket_event == nullptr ||
            websocket_event->socket != socket ||
            websocket_event->isText != detail::kFalse)
        {
            return;
        }

        QueueEntry entry;
        entry.kind = QueueEntryKind::Message;
        entry.socket = websocket_event->socket;
        if (websocket_event->numBytes != 0)
        {
            if (websocket_event->data == nullptr)
                return;

            entry.payload.assign(websocket_event->data,
                                 websocket_event->data +
                                     websocket_event->numBytes);
        }
        enqueue(std::move(entry));
    }

    void handle_disconnect(WebSocketHandle socket_handle)
    {
        if (socket_handle != socket)
            return;

        enqueue_disconnect(socket_handle);
    }

    void enqueue_disconnect(WebSocketHandle socket_handle)
    {
        QueueEntry entry;
        entry.kind = QueueEntryKind::Disconnect;
        entry.socket = socket_handle;
        enqueue(std::move(entry));
    }

    void request_close_socket(WebSocketHandle socket_handle) const noexcept
    {
        if (socket_handle <= 0)
            return;

        const detail::EmscriptenWebSocketApi& api =
            detail::emscripten_websocket_api();
        if (api.close != nullptr)
            (void)api.close(socket_handle, 1000, nullptr);
    }

    void dispose_socket() noexcept
    {
        if (socket <= 0)
            return;

        const WebSocketHandle retiring_socket = socket;
        socket = 0;
        request_close_socket(retiring_socket);

        const detail::EmscriptenWebSocketApi& api =
            detail::emscripten_websocket_api();
        if (api.destroy != nullptr)
            (void)api.destroy(retiring_socket);
    }

    void clear_queue()
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        queue.clear();
    }

    void enqueue(QueueEntry entry)
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        queue.push_back(std::move(entry));
    }

    std::string url;
    Options options;
    WebSocketHandle socket = 0;
    bool started = false;
    bool connected = false;

    std::mutex queue_mutex;
    std::deque<QueueEntry> queue;
};

EmscriptenWebSocketTransport::EmscriptenWebSocketTransport(std::string url)
    : EmscriptenWebSocketTransport(std::move(url), Options{})
{
}

EmscriptenWebSocketTransport::EmscriptenWebSocketTransport(std::string url,
                                                           Options options)
    : impl_(std::make_unique<Impl>(std::move(url), std::move(options)))
{
}

EmscriptenWebSocketTransport::~EmscriptenWebSocketTransport() = default;

void EmscriptenWebSocketTransport::send(PeerId peer_id,
                                        const std::uint8_t* data,
                                        std::size_t len)
{
    impl_->send(peer_id, data, len);
}

std::vector<ReceivedMessage> EmscriptenWebSocketTransport::poll()
{
    return impl_->poll();
}

void EmscriptenWebSocketTransport::accept_connections()
{
    impl_->accept_connections();
}

void EmscriptenWebSocketTransport::disconnect(PeerId peer_id)
{
    impl_->disconnect(peer_id);
}

std::vector<PeerId> EmscriptenWebSocketTransport::connected_peers() const
{
    return impl_->connected_peers();
}

} // namespace og::sim
