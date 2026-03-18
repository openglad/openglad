#include <openglad/platform/net_transport_websocket_client.h>

#include "net_transport_websocket_common.h"

#include <ixwebsocket/IXWebSocket.h>

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

        websocket.setUrl(url);
        if (options.automatic_reconnection)
            websocket.enableAutomaticReconnection();
        else
            websocket.disableAutomaticReconnection();

        websocket.setMinWaitBetweenReconnectionRetries(
            options.min_reconnect_wait_ms);
        websocket.setMaxWaitBetweenReconnectionRetries(
            options.max_reconnect_wait_ms);
        websocket.setOnMessageCallback(
            [this](const ix::WebSocketMessagePtr& message) {
                handle_message(message);
            });
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

    void handle_message(const ix::WebSocketMessagePtr& message)
    {
        if (!message)
            return;

        switch (message->type)
        {
        case ix::WebSocketMessageType::Open:
        {
            QueueEntry entry;
            entry.kind = QueueEntryKind::Connect;
            enqueue(std::move(entry));
            break;
        }

        case ix::WebSocketMessageType::Message:
        {
            if (!message->binary)
                return;

            QueueEntry entry;
            entry.kind = QueueEntryKind::Message;
            entry.payload.assign(message->str.begin(), message->str.end());
            enqueue(std::move(entry));
            break;
        }

        case ix::WebSocketMessageType::Error:
        case ix::WebSocketMessageType::Close:
        {
            QueueEntry entry;
            entry.kind = QueueEntryKind::Disconnect;
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

        websocket.start();
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

        if (peer_id != options.remote_peer_id || !connected)
            return;

        const char* bytes = reinterpret_cast<const char*>(data);
        const std::string payload =
            (bytes == nullptr || len == 0) ? std::string()
                                           : std::string(bytes, bytes + len);
        const ix::WebSocketSendInfo send_info = websocket.sendBinary(payload);
        if (!send_info.success)
        {
            enqueue_disconnect();
            websocket.close();
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

        connected = false;
        if (!started)
            return;

        websocket.stop();
        started = false;
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
            websocket.stop();
    }

private:
    void enqueue_disconnect()
    {
        QueueEntry entry;
        entry.kind = QueueEntryKind::Disconnect;
        enqueue(std::move(entry));
    }

    void enqueue(QueueEntry entry)
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        queue.push_back(std::move(entry));
    }

    detail::IxNetSystemGuard net_system_guard;
    std::string url;
    Options options;
    ix::WebSocket websocket;
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
