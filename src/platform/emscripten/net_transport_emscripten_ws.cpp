#include "net_transport_emscripten_ws.h"

#include <emscripten/websocket.h>

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

const char* emscripten_result_name(EMSCRIPTEN_RESULT result) noexcept
{
    switch (result)
    {
    case EMSCRIPTEN_RESULT_SUCCESS:
        return "EMSCRIPTEN_RESULT_SUCCESS";
    case EMSCRIPTEN_RESULT_DEFERRED:
        return "EMSCRIPTEN_RESULT_DEFERRED";
    case EMSCRIPTEN_RESULT_NOT_SUPPORTED:
        return "EMSCRIPTEN_RESULT_NOT_SUPPORTED";
    case EMSCRIPTEN_RESULT_FAILED_NOT_DEFERRED:
        return "EMSCRIPTEN_RESULT_FAILED_NOT_DEFERRED";
    case EMSCRIPTEN_RESULT_INVALID_TARGET:
        return "EMSCRIPTEN_RESULT_INVALID_TARGET";
    case EMSCRIPTEN_RESULT_UNKNOWN_TARGET:
        return "EMSCRIPTEN_RESULT_UNKNOWN_TARGET";
    case EMSCRIPTEN_RESULT_INVALID_PARAM:
        return "EMSCRIPTEN_RESULT_INVALID_PARAM";
    case EMSCRIPTEN_RESULT_FAILED:
        return "EMSCRIPTEN_RESULT_FAILED";
    case EMSCRIPTEN_RESULT_NO_DATA:
        return "EMSCRIPTEN_RESULT_NO_DATA";
    case EMSCRIPTEN_RESULT_TIMED_OUT:
        return "EMSCRIPTEN_RESULT_TIMED_OUT";
    default:
        return "EMSCRIPTEN_RESULT_UNKNOWN";
    }
}

[[noreturn]] void throw_emscripten_error(std::string_view operation,
                                         EMSCRIPTEN_RESULT result)
{
    throw std::runtime_error(std::format(
        "EmscriptenWebSocketTransport {} failed: {} ({})",
        operation,
        emscripten_result_name(result),
        result));
}

void throw_if_emscripten_failed(std::string_view operation,
                                EMSCRIPTEN_RESULT result)
{
    if (result != EMSCRIPTEN_RESULT_SUCCESS)
        throw_emscripten_error(operation, result);
}

} // namespace

struct EmscriptenWebSocketTransport::Impl
{
    enum class QueueEntryKind : std::uint8_t {
        Connect,
        Message,
        Disconnect,
    };

    struct QueueEntry {
        QueueEntryKind kind = QueueEntryKind::Message;
        EMSCRIPTEN_WEBSOCKET_T socket = 0;
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

        if (!emscripten_websocket_is_supported())
        {
            throw std::runtime_error(
                "EmscriptenWebSocketTransport requires browser WebSocket support");
        }

        clear_queue();
        connected = false;

        EmscriptenWebSocketCreateAttributes create_attributes;
        emscripten_websocket_init_create_attributes(&create_attributes);
        create_attributes.url = url.c_str();
        create_attributes.protocols =
            options.protocols.empty() ? nullptr : options.protocols.c_str();

        const EMSCRIPTEN_WEBSOCKET_T new_socket =
            emscripten_websocket_new(&create_attributes);
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
                emscripten_websocket_set_onopen_callback(
                    socket, this, &Impl::on_open));
            throw_if_emscripten_failed(
                "setting onmessage callback",
                emscripten_websocket_set_onmessage_callback(
                    socket, this, &Impl::on_message));
            throw_if_emscripten_failed(
                "setting onerror callback",
                emscripten_websocket_set_onerror_callback(
                    socket, this, &Impl::on_error));
            throw_if_emscripten_failed(
                "setting onclose callback",
                emscripten_websocket_set_onclose_callback(
                    socket, this, &Impl::on_close));
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

        unsigned short ready_state = 0;
        const EMSCRIPTEN_RESULT ready_state_result =
            emscripten_websocket_get_ready_state(socket, &ready_state);
        if (ready_state_result != EMSCRIPTEN_RESULT_SUCCESS ||
            ready_state != kWebSocketReadyStateOpen)
        {
            enqueue_disconnect(socket);
            request_close_socket(socket);
            return;
        }

        std::uint8_t empty_payload = 0;
        void* payload = len == 0 ? static_cast<void*>(&empty_payload)
                                 : const_cast<std::uint8_t*>(data);
        const EMSCRIPTEN_RESULT send_result = emscripten_websocket_send_binary(
            socket, payload, static_cast<std::uint32_t>(len));
        if (send_result != EMSCRIPTEN_RESULT_SUCCESS)
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
    static EM_BOOL on_open(int,
                           const EmscriptenWebSocketOpenEvent* websocket_event,
                           void* user_data)
    {
        if (websocket_event == nullptr || user_data == nullptr)
            return EM_FALSE;

        static_cast<Impl*>(user_data)->handle_connect(websocket_event->socket);
        return EM_TRUE;
    }

    static EM_BOOL on_message(
        int,
        const EmscriptenWebSocketMessageEvent* websocket_event,
        void* user_data)
    {
        if (websocket_event == nullptr || user_data == nullptr)
            return EM_FALSE;

        static_cast<Impl*>(user_data)->handle_message(websocket_event);
        return EM_TRUE;
    }

    static EM_BOOL on_error(
        int,
        const EmscriptenWebSocketErrorEvent* websocket_event,
        void* user_data)
    {
        if (websocket_event == nullptr || user_data == nullptr)
            return EM_FALSE;

        static_cast<Impl*>(user_data)->handle_disconnect(websocket_event->socket);
        return EM_TRUE;
    }

    static EM_BOOL on_close(
        int,
        const EmscriptenWebSocketCloseEvent* websocket_event,
        void* user_data)
    {
        if (websocket_event == nullptr || user_data == nullptr)
            return EM_FALSE;

        static_cast<Impl*>(user_data)->handle_disconnect(websocket_event->socket);
        return EM_TRUE;
    }

    void handle_connect(EMSCRIPTEN_WEBSOCKET_T socket_handle)
    {
        if (socket_handle != socket)
            return;

        QueueEntry entry;
        entry.kind = QueueEntryKind::Connect;
        entry.socket = socket_handle;
        enqueue(std::move(entry));
    }

    void handle_message(const EmscriptenWebSocketMessageEvent* websocket_event)
    {
        if (websocket_event == nullptr ||
            websocket_event->socket != socket ||
            websocket_event->isText)
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

    void handle_disconnect(EMSCRIPTEN_WEBSOCKET_T socket_handle)
    {
        if (socket_handle != socket)
            return;

        enqueue_disconnect(socket_handle);
    }

    void enqueue_disconnect(EMSCRIPTEN_WEBSOCKET_T socket_handle)
    {
        QueueEntry entry;
        entry.kind = QueueEntryKind::Disconnect;
        entry.socket = socket_handle;
        enqueue(std::move(entry));
    }

    void request_close_socket(EMSCRIPTEN_WEBSOCKET_T socket_handle) const noexcept
    {
        if (socket_handle <= 0)
            return;

        (void)emscripten_websocket_close(socket_handle, 1000, nullptr);
    }

    void dispose_socket() noexcept
    {
        if (socket <= 0)
            return;

        const EMSCRIPTEN_WEBSOCKET_T retiring_socket = socket;
        socket = 0;
        request_close_socket(retiring_socket);
        (void)emscripten_websocket_delete(retiring_socket);
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
    EMSCRIPTEN_WEBSOCKET_T socket = 0;
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
