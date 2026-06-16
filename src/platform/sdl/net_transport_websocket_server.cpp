#include <openglad/platform/net_transport_websocket_server.h>

#include "net_transport_websocket_common.h"

#include <ixwebsocket/IXWebSocketServer.h>

#include <algorithm>
#include <deque>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace og::sim {
namespace {

constexpr std::size_t kMaxInboundFrameBytes = 128u * 1024u;
constexpr std::size_t kMaxQueuedMessages = 1024u;
constexpr std::size_t kMaxQueuedPayloadBytes = 16u * 1024u * 1024u;

} // namespace

struct WebSocketServerTransport::Impl
{
    struct PeerState {
        std::string connection_id;
        std::weak_ptr<ix::WebSocket> socket;
    };

    enum class QueueEntryKind : std::uint8_t {
        Connect,
        Message,
        Disconnect,
    };

    struct QueueEntry {
        QueueEntryKind kind = QueueEntryKind::Message;
        std::string connection_id;
        std::shared_ptr<ix::WebSocket> socket;
        std::vector<std::uint8_t> payload;
    };

    Impl(int port_in, Options options_in)
        : net_system_guard()
        , options(std::move(options_in))
        , port(port_in)
        , server(port, options.host, options.backlog, options.max_connections)
    {
        server.setOnClientMessageCallback(
            [this](std::shared_ptr<ix::ConnectionState> connection_state,
                   ix::WebSocket& websocket,
                   const ix::WebSocketMessagePtr& message) {
                handle_client_message(std::move(connection_state), websocket, message);
            });
    }

    void handle_client_message(
        const std::shared_ptr<ix::ConnectionState>& connection_state,
        ix::WebSocket& websocket,
        const ix::WebSocketMessagePtr& message)
    {
        if (!message || !connection_state)
            return;

        switch (message->type)
        {
        case ix::WebSocketMessageType::Open:
        {
            QueueEntry entry;
            entry.kind = QueueEntryKind::Connect;
            entry.connection_id = connection_state->getId();
            entry.socket = resolve_socket(websocket);
            if (!entry.socket)
                return;
            enqueue(std::move(entry));
            break;
        }

        case ix::WebSocketMessageType::Message:
        {
            if (!message->binary)
                return;
            if (message->str.size() > kMaxInboundFrameBytes)
            {
                websocket.close(1009, "message too large");
                enqueue_disconnect(connection_state->getId());
                return;
            }

            QueueEntry entry;
            entry.kind = QueueEntryKind::Message;
            entry.connection_id = connection_state->getId();
            entry.payload.assign(message->str.begin(), message->str.end());
            if (!enqueue(std::move(entry)))
            {
                websocket.close(1008, "receive queue full");
                enqueue_disconnect(connection_state->getId());
            }
            break;
        }

        case ix::WebSocketMessageType::Error:
        case ix::WebSocketMessageType::Close:
        {
            QueueEntry entry;
            entry.kind = QueueEntryKind::Disconnect;
            entry.connection_id = connection_state->getId();
            enqueue(std::move(entry));
            break;
        }

        default:
            break;
        }
    }

    void accept_connections()
    {
        if (listening)
            return;

        if (!server.listenAndStart())
        {
            throw std::runtime_error(std::format(
                "WebSocketServerTransport failed to listen on {}:{}",
                options.host,
                port));
        }

        listening = true;
    }

    void send(PeerId peer_id, const std::uint8_t* data, std::size_t len)
    {
        if (data == nullptr && len != 0)
        {
            throw std::runtime_error(std::format(
                "WebSocketServerTransport peer {} send buffer is null",
                peer_id));
        }

        const auto peer_it = peers.find(peer_id);
        if (peer_it == peers.end())
            return;

        const std::shared_ptr<ix::WebSocket> socket = peer_it->second.socket.lock();
        if (!socket)
        {
            enqueue_disconnect(peer_it->second.connection_id);
            return;
        }

        const char* bytes = reinterpret_cast<const char*>(data);
        const std::size_t send_len = (bytes == nullptr || len == 0) ? 0 : len;
        const ix::WebSocketSendInfo send_info =
            socket->sendBinary(ix::IXWebSocketSendData(bytes, send_len));
        if (!send_info.success)
        {
            enqueue_disconnect(peer_it->second.connection_id);
            socket->close();
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
            queued_message_count = 0;
            queued_payload_bytes = 0;
        }

        std::vector<ReceivedMessage> drained_messages;
        drained_messages.reserve(queued_entries.size());
        for (auto& entry : queued_entries)
        {
            switch (entry.kind)
            {
            case QueueEntryKind::Connect:
            {
                if (!entry.socket ||
                    connection_to_peer_id.contains(entry.connection_id))
                {
                    break;
                }

                if (next_peer_id == std::numeric_limits<PeerId>::max())
                {
                    // PeerId space exhausted; the next increment would wrap to
                    // the reserved 'no peer' sentinel (0). Drop the connection
                    // rather than register a ghost peer.
                    break; // GCOVR_EXCL_LINE -- unreachable without 2^32 live peers
                }

                const PeerId peer_id = next_peer_id++;
                connection_to_peer_id.emplace(entry.connection_id, peer_id);
                peers.emplace(peer_id,
                              PeerState{
                                  .connection_id = std::move(entry.connection_id),
                                  .socket = entry.socket,
                              });
                break;
            }

            case QueueEntryKind::Message:
            {
                const auto peer_it =
                    connection_to_peer_id.find(entry.connection_id);
                if (peer_it == connection_to_peer_id.end())
                    break;

                drained_messages.push_back(
                    {peer_it->second, std::move(entry.payload)});
                break;
            }

            case QueueEntryKind::Disconnect:
            {
                const auto peer_it =
                    connection_to_peer_id.find(entry.connection_id);
                if (peer_it == connection_to_peer_id.end())
                    break;

                peers.erase(peer_it->second);
                connection_to_peer_id.erase(peer_it);
                break;
            }
            }
        }

        return drained_messages;
    }

    void disconnect(PeerId peer_id)
    {
        const auto peer_it = peers.find(peer_id);
        if (peer_it == peers.end())
            return;

        const std::shared_ptr<ix::WebSocket> socket = peer_it->second.socket.lock();
        connection_to_peer_id.erase(peer_it->second.connection_id);
        peers.erase(peer_it);

        if (socket)
            socket->close();
    }

    std::vector<PeerId> connected_peers() const
    {
        std::vector<PeerId> result;
        result.reserve(peers.size());
        for (const auto& [peer_id, peer] : peers)
            result.push_back(peer_id);
        std::sort(result.begin(), result.end());
        return result;
    }

    ~Impl()
    {
        if (listening)
            server.stop();
    }

private:
    void enqueue_disconnect(const std::string& connection_id)
    {
        QueueEntry entry;
        entry.kind = QueueEntryKind::Disconnect;
        entry.connection_id = connection_id;
        enqueue(std::move(entry));
    }

    bool enqueue(QueueEntry entry)
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (queue.size() >= kMaxQueuedMessages)
            return false;
        if (entry.kind == QueueEntryKind::Message)
        {
            if (queued_message_count >= kMaxQueuedMessages ||
                entry.payload.size() >
                    kMaxQueuedPayloadBytes - queued_payload_bytes)
            {
                return false;
            }
            ++queued_message_count;
            queued_payload_bytes += entry.payload.size();
        }
        queue.push_back(std::move(entry));
        return true;
    }

    std::shared_ptr<ix::WebSocket> resolve_socket(ix::WebSocket& websocket)
    {
        for (const auto& client : server.getClients())
        {
            if (client.get() == &websocket)
                return client;
        }
        return {};
    }

    detail::IxNetSystemGuard net_system_guard;
    Options options;
    int port = 0;
    ix::WebSocketServer server;
    bool listening = false;

    std::mutex queue_mutex;
    std::deque<QueueEntry> queue;
    std::size_t queued_message_count = 0;
    std::size_t queued_payload_bytes = 0;

    PeerId next_peer_id = 1;
    std::unordered_map<std::string, PeerId> connection_to_peer_id;
    std::unordered_map<PeerId, PeerState> peers;
};

WebSocketServerTransport::WebSocketServerTransport(int port)
    : WebSocketServerTransport(port, Options{})
{
}

WebSocketServerTransport::WebSocketServerTransport(int port, Options options)
    : impl_(std::make_unique<Impl>(port, std::move(options)))
{
}

WebSocketServerTransport::~WebSocketServerTransport() = default;

void WebSocketServerTransport::send(PeerId peer_id,
                                    const std::uint8_t* data,
                                    std::size_t len)
{
    impl_->send(peer_id, data, len);
}

std::vector<ReceivedMessage> WebSocketServerTransport::poll()
{
    return impl_->poll();
}

void WebSocketServerTransport::accept_connections()
{
    impl_->accept_connections();
}

void WebSocketServerTransport::disconnect(PeerId peer_id)
{
    impl_->disconnect(peer_id);
}

std::vector<PeerId> WebSocketServerTransport::connected_peers() const
{
    return impl_->connected_peers();
}

} // namespace og::sim
