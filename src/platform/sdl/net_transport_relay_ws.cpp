#include <openglad/platform/net_transport_relay_ws.h>

#include "net_transport_websocket_common.h"

#include <ixwebsocket/IXWebSocket.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <deque>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace og::sim {
namespace {

constexpr std::uint8_t kRelaySendToPeerTag = 1u;
constexpr std::uint8_t kRelayReceiveFromPeerTag = 2u;
constexpr std::uint8_t kRelayBroadcastTag = 3u;
constexpr std::size_t kRelayPeerHeaderSize = 5u;
constexpr std::size_t kMaxInboundFrameBytes = 128u * 1024u;
constexpr std::size_t kMaxInboundTextBytes = 8u * 1024u;
constexpr std::size_t kMaxQueuedMessages = 1024u;
constexpr std::size_t kMaxQueuedPayloadBytes = 16u * 1024u * 1024u;

std::string trim_copy(std::string_view text)
{
    const auto not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    const auto begin =
        std::find_if(text.begin(), text.end(), not_space);
    const auto end =
        std::find_if(text.rbegin(), text.rend(), not_space).base();
    if (begin >= end)
        return {};
    return std::string(begin, end);
}

std::optional<std::size_t> find_json_field_value(
    std::string_view text,
    std::string_view key)
{
    const std::string needle = std::format("\"{}\"", key);
    const std::size_t key_pos = text.find(needle);
    if (key_pos == std::string_view::npos)
        return std::nullopt;

    std::size_t colon_pos = text.find(':', key_pos + needle.size());
    if (colon_pos == std::string_view::npos)
        return std::nullopt;
    ++colon_pos;

    while (colon_pos < text.size() &&
           std::isspace(static_cast<unsigned char>(text[colon_pos])))
    {
        ++colon_pos;
    }
    if (colon_pos >= text.size())
        return std::nullopt;
    return colon_pos;
}

std::optional<std::string> extract_json_string_field(
    std::string_view text,
    std::string_view key)
{
    const auto value_pos = find_json_field_value(text, key);
    if (!value_pos.has_value() || text[*value_pos] != '"')
        return std::nullopt;

    std::string result;
    bool escape = false;
    for (std::size_t index = *value_pos + 1; index < text.size(); ++index)
    {
        const char ch = text[index];
        if (escape)
        {
            result.push_back(ch);
            escape = false;
            continue;
        }
        if (ch == '\\')
        {
            escape = true;
            continue;
        }
        if (ch == '"')
            return result;
        result.push_back(ch);
    }

    return std::nullopt;
}

std::optional<std::uint32_t> extract_json_u32_field(
    std::string_view text,
    std::string_view key)
{
    const auto value_pos = find_json_field_value(text, key);
    if (!value_pos.has_value())
        return std::nullopt;

    std::size_t end = *value_pos;
    while (end < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[end])))
    {
        ++end;
    }
    if (end == *value_pos)
        return std::nullopt;

    std::uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(
        text.data() + *value_pos,
        text.data() + end,
        value);
    if (ec != std::errc{} || ptr != text.data() + end)
        return std::nullopt;
    return value;
}

std::optional<bool> extract_json_bool_field(std::string_view text,
                                            std::string_view key)
{
    const auto value_pos = find_json_field_value(text, key);
    if (!value_pos.has_value())
        return std::nullopt;

    const std::string_view rest = text.substr(*value_pos);
    if (rest.rfind("true", 0) == 0)
        return true;
    if (rest.rfind("false", 0) == 0)
        return false;
    return std::nullopt;
}

std::vector<PeerId> extract_json_u32_array_field(std::string_view text,
                                                 std::string_view key)
{
    const auto value_pos = find_json_field_value(text, key);
    if (!value_pos.has_value() || text[*value_pos] != '[')
        return {};

    std::vector<PeerId> result;
    std::size_t cursor = *value_pos + 1;
    while (cursor < text.size())
    {
        while (cursor < text.size() &&
               std::isspace(static_cast<unsigned char>(text[cursor])))
        {
            ++cursor;
        }

        if (cursor >= text.size() || text[cursor] == ']')
            break;

        const std::size_t start = cursor;
        while (cursor < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[cursor])))
        {
            ++cursor;
        }
        if (cursor == start)
            break;

        std::uint32_t value = 0;
        const auto [ptr, ec] = std::from_chars(
            text.data() + start,
            text.data() + cursor,
            value);
        if (ec == std::errc{} && ptr == text.data() + cursor)
            result.push_back(value);

        while (cursor < text.size() &&
               std::isspace(static_cast<unsigned char>(text[cursor])))
        {
            ++cursor;
        }
        if (cursor < text.size() && text[cursor] == ',')
            ++cursor;
    }

    return result;
}

void append_u32_le(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

std::optional<std::uint32_t> read_u32_le(std::span<const std::uint8_t> bytes,
                                         std::size_t offset)
{
    if (offset + sizeof(std::uint32_t) > bytes.size())
        return std::nullopt;

    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::vector<std::uint8_t> encode_targeted_relay_payload(
    PeerId peer_id,
    const std::uint8_t* data,
    std::size_t len)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(kRelayPeerHeaderSize + len);
    payload.push_back(kRelaySendToPeerTag);
    append_u32_le(payload, peer_id);
    if (data != nullptr && len != 0)
        payload.insert(payload.end(), data, data + len);
    return payload;
}

std::vector<std::uint8_t> encode_broadcast_relay_payload(
    const std::uint8_t* data,
    std::size_t len)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(1u + len);
    payload.push_back(kRelayBroadcastTag);
    if (data != nullptr && len != 0)
        payload.insert(payload.end(), data, data + len);
    return payload;
}

std::optional<ReceivedMessage> decode_incoming_relay_payload(
    std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < kRelayPeerHeaderSize ||
        bytes[0] != kRelayReceiveFromPeerTag)
    {
        return std::nullopt;
    }

    const auto peer_id = read_u32_le(bytes, 1u);
    if (!peer_id.has_value() || *peer_id == 0)
        return std::nullopt;

    ReceivedMessage message;
    message.peer_id = *peer_id;
    message.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kRelayPeerHeaderSize),
                        bytes.end());
    return message;
}

} // namespace

struct RelayWebSocketTransport::Impl
{
    enum class QueueEntryKind : std::uint8_t {
        Connect,
        TextMessage,
        BinaryMessage,
        Disconnect,
    };

    struct QueueEntry {
        QueueEntryKind kind = QueueEntryKind::BinaryMessage;
        std::uint64_t generation = 0;
        std::string text;
        std::vector<std::uint8_t> payload;
    };

    Impl(std::string url_in, Options options_in)
        : net_system_guard()
        , url(trim_copy(url_in))
        , options(normalize_options(std::move(options_in)))
    {
        if (url.empty())
            throw std::invalid_argument("RelayWebSocketTransport URL must not be empty");
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

    // Runs on the ix::WebSocket callback thread. `socket` is the socket that
    // delivered `message`; it is alive for the whole call because the
    // callback runs on that socket's own background thread, which is joined
    // (websocket->stop()) before the socket is destroyed. This thread must
    // never read the `websocket` member: that raced the game thread tearing
    // the handle down in ~Impl().
    void handle_message(std::uint64_t generation,
                        ix::WebSocket& socket,
                        const ix::WebSocketMessagePtr& message)
    {
        if (!message)
            return;

        QueueEntry entry;
        entry.generation = generation;

        switch (message->type)
        {
        case ix::WebSocketMessageType::Open:
            entry.kind = QueueEntryKind::Connect;
            enqueue(std::move(entry));
            break;

        case ix::WebSocketMessageType::Message:
            if (message->binary)
            {
                if (message->str.size() > kMaxInboundFrameBytes)
                {
                    enqueue_disconnect(generation);
                    socket.close(1009, "message too large");
                    return;
                }
                entry.kind = QueueEntryKind::BinaryMessage;
                entry.payload.assign(message->str.begin(), message->str.end());
            }
            else
            {
                if (message->str.size() > kMaxInboundTextBytes)
                {
                    enqueue_disconnect(generation);
                    socket.close(1009, "control message too large");
                    return;
                }
                entry.kind = QueueEntryKind::TextMessage;
                entry.text = message->str;
            }
            if (!enqueue(std::move(entry)))
            {
                enqueue_disconnect(generation);
                socket.close(1008, "receive queue full");
            }
            break;

        case ix::WebSocketMessageType::Error:
        case ix::WebSocketMessageType::Close:
            entry.kind = QueueEntryKind::Disconnect;
            enqueue(std::move(entry));
            break;

        default:
            break;
        }
    }

    void accept_connections()
    {
        if (started)
            return;

        clear_queue();
        reset_peer_state();
        connected = false;
        ever_connected = false;
        link_closed = false;
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
                "RelayWebSocketTransport peer {} send buffer is null",
                peer_id));
        }

        if (peer_id == 0 || !connected || !websocket ||
            blocked_peers.contains(peer_id) || !remote_peers.contains(peer_id))
        {
            return;
        }

        const std::vector<std::uint8_t> payload =
            encode_targeted_relay_payload(peer_id, data, len);
        const ix::WebSocketSendInfo send_info = websocket->sendBinary(payload);
        if (!send_info.success)
        {
            enqueue_disconnect(active_generation);
            websocket->close();
        }
    }

    void broadcast(const std::uint8_t* data, std::size_t len)
    {
        if (data == nullptr && len != 0)
        {
            throw std::runtime_error(
                "RelayWebSocketTransport broadcast buffer is null");
        }

        if (!connected || !websocket || remote_peers.empty())
            return;

        const std::vector<std::uint8_t> payload =
            encode_broadcast_relay_payload(data, len);
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
            if (!lock.owns_lock() || queue.empty())
                return {};
            queued_entries.swap(queue);
            queued_message_count = 0;
            queued_payload_bytes = 0;
        }

        std::vector<ReceivedMessage> received;
        for (auto& entry : queued_entries)
        {
            if (entry.generation != active_generation)
                continue;

            switch (entry.kind)
            {
            case QueueEntryKind::Connect:
                connected = true;
                ever_connected = true;
                link_closed = false;
                reset_peer_state();
                break;

            case QueueEntryKind::TextMessage:
                if (connected)
                    process_control_message(entry.text);
                break;

            case QueueEntryKind::BinaryMessage:
                if (!connected)
                    break;
                if (auto decoded =
                        decode_incoming_relay_payload(entry.payload))
                {
                    if (!blocked_peers.contains(decoded->peer_id))
                        received.push_back(std::move(*decoded));
                }
                break;

            case QueueEntryKind::Disconnect:
                connected = false;
                link_closed = true;
                reset_peer_state();
                break;
            }
        }

        return received;
    }

    void disconnect(PeerId peer_id)
    {
        if (peer_id == 0)
            return;

        remote_peers.erase(peer_id);
        blocked_peers.insert(peer_id);
        if (host_peer_id_.has_value() && *host_peer_id_ == peer_id)
            host_peer_id_.reset();
    }

    std::vector<PeerId> connected_peers() const
    {
        std::vector<PeerId> peers(remote_peers.begin(), remote_peers.end());
        std::sort(peers.begin(), peers.end());
        return peers;
    }

    std::optional<PeerId> local_peer_id() const noexcept
    {
        return local_peer_id_;
    }

    std::optional<PeerId> host_peer_id() const noexcept
    {
        return host_peer_id_;
    }

    // Reads the connection flags maintained by poll() on the game thread; the
    // ix callbacks only enqueue transitions, so this is game-thread state.
    TransportLinkState link_state() const noexcept
    {
        if (connected)
            return TransportLinkState::Connected;
        if (link_closed)
        {
            return ever_connected ? TransportLinkState::Lost
                                  : TransportLinkState::Failed;
        }
        return TransportLinkState::Connecting;
    }

    ~Impl()
    {
        active_generation = 0;
        // Teardown protocol: quiesce the callback source before the socket
        // handle dies. stop() joins the socket's background thread — the
        // only thread that runs handle_message() — so once it returns no
        // callback can race the member teardown that follows this body.
        // (Moving the pointer out before stopping raced the callback
        // thread's reads of `websocket`.)
        if (websocket)
            websocket->stop();
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
        // The callback receives the socket that owns the callback thread, so
        // handle_message() never has to read the cross-thread `websocket`
        // member. The raw pointer cannot dangle: the callback only runs on
        // this socket's background thread, and teardown joins that thread
        // (stop()) before destroying the socket.
        ix::WebSocket* raw_socket = socket.get();
        socket->setOnMessageCallback(
            [this, generation, raw_socket](
                const ix::WebSocketMessagePtr& message) {
                handle_message(generation, *raw_socket, message);
            });
        return socket;
    }

    void process_control_message(const std::string& text)
    {
        const std::optional<std::string> type =
            extract_json_string_field(text, "type");
        if (!type.has_value())
            return;

        if (*type == "joined")
        {
            local_peer_id_ = extract_json_u32_field(text, "peer_id");
            if (const auto host = extract_json_u32_field(text, "host");
                host.has_value() && *host != 0)
            {
                host_peer_id_ = *host;
            }
            return;
        }

        if (*type == "peer_list")
        {
            std::unordered_set<PeerId> next_peers;
            for (const PeerId peer_id : extract_json_u32_array_field(text, "peers"))
            {
                if (peer_id == 0 || blocked_peers.contains(peer_id))
                    continue;
                if (local_peer_id_.has_value() && *local_peer_id_ == peer_id)
                    continue;
                next_peers.insert(peer_id);
            }
            remote_peers = std::move(next_peers);
            if (const auto host = extract_json_u32_field(text, "host");
                host.has_value() && *host != 0)
            {
                host_peer_id_ = *host;
                if ((!local_peer_id_.has_value() || *local_peer_id_ != *host) &&
                    !blocked_peers.contains(*host))
                {
                    remote_peers.insert(*host);
                }
            }
            return;
        }

        if (*type == "peer_joined")
        {
            const auto peer_id = extract_json_u32_field(text, "peer_id");
            if (!peer_id.has_value() || *peer_id == 0 ||
                blocked_peers.contains(*peer_id))
            {
                return;
            }

            if (!local_peer_id_.has_value() || *local_peer_id_ != *peer_id)
                remote_peers.insert(*peer_id);

            if (const auto is_host =
                    extract_json_bool_field(text, "is_host");
                is_host.value_or(false))
            {
                host_peer_id_ = *peer_id;
            }
            return;
        }

        if (*type == "peer_left")
        {
            const auto peer_id = extract_json_u32_field(text, "peer_id");
            if (!peer_id.has_value())
                return;
            remote_peers.erase(*peer_id);
            if (host_peer_id_.has_value() && *host_peer_id_ == *peer_id)
                host_peer_id_.reset();
            return;
        }

        if (*type == "host_changed")
        {
            const auto peer_id = extract_json_u32_field(text, "new_host");
            if (!peer_id.has_value() || *peer_id == 0)
                return;
            host_peer_id_ = *peer_id;
            if ((!local_peer_id_.has_value() || *local_peer_id_ != *peer_id) &&
                !blocked_peers.contains(*peer_id))
            {
                remote_peers.insert(*peer_id);
            }
        }
    }

    void reset_peer_state()
    {
        local_peer_id_.reset();
        host_peer_id_.reset();
        remote_peers.clear();
        blocked_peers.clear();
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
        queued_message_count = 0;
        queued_payload_bytes = 0;
    }

    bool enqueue(QueueEntry entry)
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (queue.size() >= kMaxQueuedMessages)
            return false;
        if (entry.kind == QueueEntryKind::TextMessage ||
            entry.kind == QueueEntryKind::BinaryMessage)
        {
            const std::size_t entry_size =
                entry.kind == QueueEntryKind::TextMessage
                    ? entry.text.size()
                    : entry.payload.size();
            if (queued_message_count >= kMaxQueuedMessages ||
                entry_size > kMaxQueuedPayloadBytes - queued_payload_bytes)
            {
                return false;
            }
            ++queued_message_count;
            queued_payload_bytes += entry_size;
        }
        queue.push_back(std::move(entry));
        return true;
    }

    detail::IxNetSystemGuard net_system_guard;
    std::string url;
    Options options;
    std::unique_ptr<ix::WebSocket> websocket;
    std::uint64_t next_generation = 1;
    std::uint64_t active_generation = 0;
    bool started = false;
    bool connected = false;
    bool ever_connected = false;
    bool link_closed = false;

    mutable std::mutex queue_mutex;
    std::deque<QueueEntry> queue;
    std::size_t queued_message_count = 0;
    std::size_t queued_payload_bytes = 0;

    std::optional<PeerId> local_peer_id_;
    std::optional<PeerId> host_peer_id_;
    std::unordered_set<PeerId> remote_peers;
    std::unordered_set<PeerId> blocked_peers;
};

RelayWebSocketTransport::RelayWebSocketTransport(std::string url)
    : RelayWebSocketTransport(std::move(url), Options{})
{
}

RelayWebSocketTransport::RelayWebSocketTransport(std::string url,
                                                 Options options)
    : impl_(std::make_unique<Impl>(std::move(url), std::move(options)))
{
}

RelayWebSocketTransport::~RelayWebSocketTransport() = default;

void RelayWebSocketTransport::send(PeerId peer_id,
                                   const std::uint8_t* data,
                                   std::size_t len)
{
    impl_->send(peer_id, data, len);
}

void RelayWebSocketTransport::broadcast(const std::uint8_t* data,
                                        std::size_t len)
{
    impl_->broadcast(data, len);
}

std::vector<ReceivedMessage> RelayWebSocketTransport::poll()
{
    return impl_->poll();
}

void RelayWebSocketTransport::accept_connections()
{
    impl_->accept_connections();
}

void RelayWebSocketTransport::disconnect(PeerId peer_id)
{
    impl_->disconnect(peer_id);
}

std::vector<PeerId> RelayWebSocketTransport::connected_peers() const
{
    return impl_->connected_peers();
}

TransportLinkState RelayWebSocketTransport::link_state() const noexcept
{
    return impl_->link_state();
}

std::optional<PeerId> RelayWebSocketTransport::local_peer_id() const noexcept
{
    return impl_->local_peer_id();
}

std::optional<PeerId> RelayWebSocketTransport::host_peer_id() const noexcept
{
    return impl_->host_peer_id();
}

} // namespace og::sim
