#include <openglad/platform/net_transport_websocket_client.h>

#include "net_transport_websocket_common.h"

#include <ixwebsocket/IXWebSocket.h>

#include <cstdint>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace og::sim {

struct WebSocketClientTransport::Impl
{
    enum class QueueEntryKind : std::uint8_t {
        Connect,
        Message,
        Disconnect,
    };

    struct QueueEntry {
        QueueEntryKind kind = QueueEntryKind::Message;
        std::uint64_t generation = 0;
        std::vector<std::uint8_t> payload;
    };

    Impl(std::string url_in, Options options_in)
        : net_system_guard()
        , url(std::move(url_in))
        , options(normalize_options(std::move(options_in)))
    {
        if (url.empty())
        {
            throw std::invalid_argument(
                "WebSocketClientTransport URL must not be empty");
        }
        if (options.remote_peer_id == 0)
        {
            throw std::invalid_argument(
                "WebSocketClientTransport remote peer id must be non-zero");
        }
    }

    static Options normalize_options(Options options)
    {
        if (options.max_reconnect_wait_ms < options.min_reconnect_wait_ms)
        {
            std::swap(options.min_reconnect_wait_ms,
                      options.max_reconnect_wait_ms);
        }
        return options;
    }

    void handle_message(std::uint64_t generation,
                        const ix::WebSocketMessagePtr& message)
    {
        if (!message)
            return;

        switch (message->type)
        {
        case ix::WebSocketMessageType::Open:
        {
            QueueEntry entry;
            entry.kind = QueueEntryKind::Connect;
            entry.generation = generation;
            enqueue(std::move(entry));
            break;
        }

        case ix::WebSocketMessageType::Message:
        {
            if (!message->binary)
                return;

            QueueEntry entry;
            entry.kind = QueueEntryKind::Message;
            entry.generation = generation;
            entry.payload.assign(message->str.begin(), message->str.end());
            enqueue(std::move(entry));
            break;
        }

        case ix::WebSocketMessageType::Error:
        case ix::WebSocketMessageType::Close:
        {
            QueueEntry entry;
            entry.kind = QueueEntryKind::Disconnect;
            entry.generation = generation;
            enqueue(std::move(entry));
            break;
        }

        default:
            break;
        }
    }

    void accept_connections()
    {
        if (started)
            return;

        clear_queue();
        connected = false;
        active_generation = next_generation++;
        websocket = make_websocket(active_generation);
        websocket->start();
        started = true;
    }

    void send(PeerId peer_id, const std::uint8_t* data, std::size_t len)
    {
        if (data == nullptr && len != 0)
        {
            throw std::runtime_error(std::format(
                "WebSocketClientTransport peer {} send buffer is null",
                peer_id));
        }

        if (peer_id != options.remote_peer_id || !connected || !websocket)
            return;

        const char* bytes = reinterpret_cast<const char*>(data);
        const std::string payload =
            (bytes == nullptr || len == 0) ? std::string()
                                           : std::string(bytes, bytes + len);
        const ix::WebSocketSendInfo send_info = websocket->sendBinary(payload);
        if (!send_info.success)
        {
            enqueue_disconnect(active_generation);
            websocket->close();
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
        for (auto& entry : queued_entries)
        {
            if (entry.generation != active_generation)
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
                break;
            }
        }

        return drained_messages;
    }

    void disconnect(PeerId peer_id)
    {
        if (peer_id != options.remote_peer_id)
            return;

        active_generation = 0;
        connected = false;
        clear_queue();
        if (!started)
            return;

        started = false;
        std::unique_ptr<ix::WebSocket> retiring_websocket = std::move(websocket);
        if (retiring_websocket)
            retiring_websocket->stop();
        clear_queue();
    }

    std::vector<PeerId> connected_peers() const
    {
        if (!connected)
            return {};
        return {options.remote_peer_id};
    }

    ~Impl()
    {
        if (started)
        {
            active_generation = 0;
            std::unique_ptr<ix::WebSocket> retiring_websocket = std::move(websocket);
            if (retiring_websocket)
                retiring_websocket->stop();
        }
    }

private:
    std::unique_ptr<ix::WebSocket> make_websocket(std::uint64_t generation)
    {
        auto socket = std::make_unique<ix::WebSocket>();
        socket->setUrl(url);
        if (options.automatic_reconnection)
            socket->enableAutomaticReconnection();
        else
            socket->disableAutomaticReconnection();

        socket->setMinWaitBetweenReconnectionRetries(
            options.min_reconnect_wait_ms);
        socket->setMaxWaitBetweenReconnectionRetries(
            options.max_reconnect_wait_ms);
        socket->setOnMessageCallback(
            [this, generation](const ix::WebSocketMessagePtr& message) {
                handle_message(generation, message);
            });
        return socket;
    }

    void enqueue_disconnect(std::uint64_t generation)
    {
        QueueEntry entry;
        entry.kind = QueueEntryKind::Disconnect;
        entry.generation = generation;
        enqueue(std::move(entry));
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

    detail::IxNetSystemGuard net_system_guard;
    std::string url;
    Options options;
    std::unique_ptr<ix::WebSocket> websocket;
    std::uint64_t next_generation = 1;
    std::uint64_t active_generation = 0;
    bool started = false;
    bool connected = false;

    std::mutex queue_mutex;
    std::deque<QueueEntry> queue;
};

WebSocketClientTransport::WebSocketClientTransport(std::string url)
    : WebSocketClientTransport(std::move(url), Options{})
{
}

WebSocketClientTransport::WebSocketClientTransport(std::string url,
                                                   Options options)
    : impl_(std::make_unique<Impl>(std::move(url), std::move(options)))
{
}

WebSocketClientTransport::~WebSocketClientTransport() = default;

void WebSocketClientTransport::send(PeerId peer_id,
                                    const std::uint8_t* data,
                                    std::size_t len)
{
    impl_->send(peer_id, data, len);
}

std::vector<ReceivedMessage> WebSocketClientTransport::poll()
{
    return impl_->poll();
}

void WebSocketClientTransport::accept_connections()
{
    impl_->accept_connections();
}

void WebSocketClientTransport::disconnect(PeerId peer_id)
{
    impl_->disconnect(peer_id);
}

std::vector<PeerId> WebSocketClientTransport::connected_peers() const
{
    return impl_->connected_peers();
}

} // namespace og::sim
